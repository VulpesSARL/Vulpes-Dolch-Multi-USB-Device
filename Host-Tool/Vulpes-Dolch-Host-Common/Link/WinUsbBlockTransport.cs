using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace VulpesDolch.Host.Common.Link
{
    /// <summary>
    /// <see cref="IBlockTransport"/> on the Windows inbox WinUSB driver: opens the Host device's link interface (interface 0, vendor
    /// specific class, bound to winusb.sys through the Microsoft OS 2.0 descriptors of the firmware) by its device interface GUID and
    /// moves one message block per bulk transfer. Windows only; a Linux version would use libusb for the same two endpoints.
    /// </summary>
    public sealed class WinUsbBlockTransport : IBlockTransport, IBulkDataWriter
    {
        /// <summary>The device interface GUID the Host firmware names in its Microsoft OS 2.0 descriptor set.</summary>
        public static readonly Guid InterfaceGuid = new Guid("4D7E3A10-0F6B-4C85-9A2E-6B1D5F0C8E31");

        private const uint DigcfPresent = 0x02;
        private const uint DigcfDeviceInterface = 0x10;
        private const uint GenericRead = 0x80000000;
        private const uint GenericWrite = 0x40000000;
        private const uint FileShareReadWrite = 0x03;
        private const uint OpenExisting = 3;
        private const uint FileFlagOverlapped = 0x40000000;
        private const uint FileAttributeNormal = 0x80;
        private const int ErrorOperationAborted = 995;
        private const int ErrorNoMoreItems = 259;
        private const int PipeTypeBulk = 2;
        private const uint PolicyShortPacketTerminate = 1;
        private const uint PolicyAutoClearStall = 2;
        private const uint PolicyPipeTransferTimeout = 3;
        private const uint PolicyRawIo = 7;
        private const uint WriteTimeoutMs = 10000;

        private readonly SafeFileHandle DeviceHandle;
        private readonly IntPtr WinUsbHandle;
        private readonly byte OutPipe;
        private readonly byte InPipe;
        private readonly byte BulkOutPipe;
        private readonly byte[] WriteBuffer = new byte[BulkConstants.BufferSize];
        private readonly object WriteLock = new object();
        private volatile bool ReadCanceled;
        private int DisposedFlag;

        private WinUsbBlockTransport(string DevicePath, SafeFileHandle DeviceHandle, IntPtr WinUsbHandle, byte OutPipe, byte InPipe, byte BulkOutPipe)
        {
            this.BulkOutPipe = BulkOutPipe;
            this.DevicePath = DevicePath;
            this.DeviceHandle = DeviceHandle;
            this.WinUsbHandle = WinUsbHandle;
            this.OutPipe = OutPipe;
            this.InPipe = InPipe;
        }

        /// <summary>The device path this transport is bound to.</summary>
        public string DevicePath { get; }

        /// <summary>The paths of all Host devices that are present and bound to WinUSB (normally one).</summary>
        public static List<string> FindDevicePaths()
        {
            List<string> Found = new List<string>();
            if (OperatingSystem.IsWindows() == false)
                return (Found);

            Guid ClassGuid = InterfaceGuid;
            IntPtr InfoSet = SetupDiGetClassDevs(ref ClassGuid, IntPtr.Zero, IntPtr.Zero, DigcfPresent | DigcfDeviceInterface);
            if (InfoSet == new IntPtr(-1))
                return (Found);

            try
            {
                for (uint Index = 0; ; Index++)
                {
                    SpDeviceInterfaceData InterfaceData = new SpDeviceInterfaceData();
                    InterfaceData.cbSize = (uint)Marshal.SizeOf<SpDeviceInterfaceData>();
                    if (SetupDiEnumDeviceInterfaces(InfoSet, IntPtr.Zero, ref ClassGuid, Index, ref InterfaceData) == false)
                    {
                        if (Marshal.GetLastWin32Error() != ErrorNoMoreItems)
                            throw new IOException("Listing the Host devices failed (Windows error " + Marshal.GetLastWin32Error() + ").");
                        break;
                    }

                    uint Required;
                    SetupDiGetDeviceInterfaceDetail(InfoSet, ref InterfaceData, IntPtr.Zero, 0, out Required, IntPtr.Zero);
                    if (Required == 0)
                        continue;

                    IntPtr Detail = Marshal.AllocHGlobal((int)Required);
                    try
                    {
                        // SP_DEVICE_INTERFACE_DETAIL_DATA_W: a DWORD size field (8 in a 64 bit process, 6 in a 32 bit one) and the path.
                        Marshal.WriteInt32(Detail, (IntPtr.Size == 8) ? 8 : 6);
                        if (SetupDiGetDeviceInterfaceDetail(InfoSet, ref InterfaceData, Detail, Required, out Required, IntPtr.Zero) == true)
                            Found.Add(Marshal.PtrToStringUni(IntPtr.Add(Detail, 4)));
                    }
                    finally
                    {
                        Marshal.FreeHGlobal(Detail);
                    }
                }
            }
            finally
            {
                SetupDiDestroyDeviceInfoList(InfoSet);
            }

            return (Found);
        }

        /// <summary>Opens the link interface of the Host device. Throws if it is not attached, not bound to WinUSB, or cannot be opened.</summary>
        public static WinUsbBlockTransport Open()
        {
            if (OperatingSystem.IsWindows() == false)
                throw new PlatformNotSupportedException("The WinUSB transport needs Windows.");

            List<string> Paths = FindDevicePaths();
            if (Paths.Count == 0)
                throw new InvalidOperationException("The Host device (WinUSB interface " + InterfaceGuid.ToString("B") + ") is not attached or not bound to WinUSB.");

            string Path = Paths[0];
            SafeFileHandle Handle = CreateFile(Path, GenericRead | GenericWrite, FileShareReadWrite, IntPtr.Zero, OpenExisting,
                FileAttributeNormal | FileFlagOverlapped, IntPtr.Zero);
            if (Handle.IsInvalid == true)
                throw new IOException("Opening the Host device failed (Windows error " + Marshal.GetLastWin32Error() + ").");

            IntPtr Interface;
            if (WinUsb_Initialize(Handle, out Interface) == false)
            {
                int Error = Marshal.GetLastWin32Error();
                Handle.Dispose();
                throw new IOException("WinUsb_Initialize failed (Windows error " + Error + ").");
            }

            byte OutId = 0;
            byte InId = 0;
            byte BulkOutId = 0;
            UsbInterfaceDescriptor Descriptor;
            if (WinUsb_QueryInterfaceSettings(Interface, 0, out Descriptor) == false)
            {
                int Error = Marshal.GetLastWin32Error();
                WinUsb_Free(Interface);
                Handle.Dispose();
                throw new IOException("WinUsb_QueryInterfaceSettings failed (Windows error " + Error + ").");
            }

            for (byte Pipe = 0; Pipe < Descriptor.bNumEndpoints; Pipe++)
            {
                WinUsbPipeInformation Information;
                if (WinUsb_QueryPipe(Interface, 0, Pipe, out Information) == false)
                    continue;
                if (Information.PipeType != PipeTypeBulk)
                    continue;

                // The link pipes are the lowest addresses (0x01 / 0x81); a second OUT pipe (0x02) is the dedicated bulk data lane.
                if ((Information.PipeId & 0x80) != 0)
                {
                    if ((InId == 0) || (Information.PipeId < InId))
                        InId = Information.PipeId;
                }
                else if (OutId == 0)
                    OutId = Information.PipeId;
                else if (Information.PipeId < OutId)
                {
                    BulkOutId = OutId;
                    OutId = Information.PipeId;
                }
                else
                    BulkOutId = Information.PipeId;
            }

            if ((InId == 0) || (OutId == 0))
            {
                WinUsb_Free(Interface);
                Handle.Dispose();
                throw new IOException("The Host device does not present one bulk IN and one bulk OUT endpoint.");
            }

            // No timeout on reads (an idle link is normal; Dispose aborts the pipe), a generous one on writes, and no raw I/O: WinUSB then
            // splits large transfers by itself and a read ends with the short packet that closes a block.
            SetPolicy(Interface, InId, PolicyPipeTransferTimeout, 0);
            SetPolicy(Interface, OutId, PolicyPipeTransferTimeout, WriteTimeoutMs);
            SetBoolPolicy(Interface, InId, PolicyAutoClearStall, true);
            SetBoolPolicy(Interface, OutId, PolicyAutoClearStall, true);
            SetBoolPolicy(Interface, InId, PolicyRawIo, false);
            SetBoolPolicy(Interface, OutId, PolicyShortPacketTerminate, false);
            WinUsb_FlushPipe(Interface, InId);

            if (BulkOutId != 0)
            {
                SetPolicy(Interface, BulkOutId, PolicyPipeTransferTimeout, 30000);
                SetBoolPolicy(Interface, BulkOutId, PolicyAutoClearStall, true);
                SetBoolPolicy(Interface, BulkOutId, PolicyShortPacketTerminate, false);
                SetBoolPolicy(Interface, BulkOutId, PolicyRawIo, false);
            }

            return (new WinUsbBlockTransport(Path, Handle, Interface, OutId, InId, BulkOutId));
        }

        private static void SetPolicy(IntPtr Interface, byte Pipe, uint Policy, uint Value)
        {
            uint Copy = Value;
            WinUsb_SetPipePolicy(Interface, Pipe, Policy, 4, ref Copy);
        }

        private static void SetBoolPolicy(IntPtr Interface, byte Pipe, uint Policy, bool Value)
        {
            byte Copy = (byte)((Value == true) ? 1 : 0);
            WinUsb_SetPipePolicyByte(Interface, Pipe, Policy, 1, ref Copy);
        }

        public void WriteBlock(ReadOnlySpan<byte> Block)
        {
            if (Volatile.Read(ref DisposedFlag) != 0)
                throw new ObjectDisposedException(nameof(WinUsbBlockTransport));
            if (Block.Length > WriteBuffer.Length)
                throw new ArgumentException("The block is longer than a transfer.", nameof(Block));

            lock (WriteLock)
            {
                Block.CopyTo(WriteBuffer);
                uint Sent;
                if (WinUsb_WritePipe(WinUsbHandle, OutPipe, WriteBuffer, (uint)Block.Length, out Sent, IntPtr.Zero) == false)
                    throw new IOException("Writing to the Host device failed (Windows error " + Marshal.GetLastWin32Error() + ").");
                if (Sent != (uint)Block.Length)
                    throw new IOException("Only " + Sent + " of " + Block.Length + " bytes were written to the Host device.");
            }
        }

        public int ReadBlock(byte[] Buffer)
        {
            if (Buffer.Length < BulkConstants.BufferSize)
                throw new ArgumentException("The buffer must hold at least " + BulkConstants.BufferSize + " bytes.", nameof(Buffer));

            while (true)
            {
                if ((ReadCanceled == true) || (Volatile.Read(ref DisposedFlag) != 0))
                    return (-1);

                uint Got;
                if (WinUsb_ReadPipe(WinUsbHandle, InPipe, Buffer, (uint)BulkConstants.BufferSize, out Got, IntPtr.Zero) == true)
                    return ((int)Got);

                int Error = Marshal.GetLastWin32Error();
                if ((ReadCanceled == true) || (Volatile.Read(ref DisposedFlag) != 0) || (Error == ErrorOperationAborted))
                    return (-1);

                throw new IOException("Reading from the Host device failed (Windows error " + Error + ").");
            }
        }

        /// <summary>Speed test only (not used by the link): raw I/O on both pipes and a timeout for the raw reads, so big transfers go through as they are.</summary>
        public void ConfigureRawStreaming(uint ReadTimeoutMs)
        {
            SetPolicy(WinUsbHandle, InPipe, PolicyPipeTransferTimeout, ReadTimeoutMs);
            SetBoolPolicy(WinUsbHandle, InPipe, PolicyRawIo, true);
            SetBoolPolicy(WinUsbHandle, OutPipe, PolicyRawIo, true);
        }

        /// <summary>Speed test only: sets the transfer timeouts of the IN and the OUT pipe (milliseconds, 0 = none).</summary>
        public void SetRawTimeouts(uint ReadTimeoutMs, uint WriteTimeoutMsValue)
        {
            SetPolicy(WinUsbHandle, InPipe, PolicyPipeTransferTimeout, ReadTimeoutMs);
            SetPolicy(WinUsbHandle, OutPipe, PolicyPipeTransferTimeout, WriteTimeoutMsValue);
        }

        public bool IsAvailable
        {
            get { return (BulkOutPipe != 0); }
        }

        public bool WriteBulk(byte[] Data, int Count)
        {
            uint Sent;

            if (BulkOutPipe == 0)
                return (false);
            if (WinUsb_WritePipe(WinUsbHandle, BulkOutPipe, Data, (uint)Count, out Sent, IntPtr.Zero) == false)
                return (false);
            return (Sent == (uint)Count);
        }

        /// <summary>Speed test only: one bulk OUT transfer of <paramref name="Length"/> bytes of <paramref name="Data"/>; returns the number of bytes sent.</summary>
        public int WriteRaw(byte[] Data, int Length)
        {
            uint Sent;
            if (WinUsb_WritePipe(WinUsbHandle, OutPipe, Data, (uint)Length, out Sent, IntPtr.Zero) == false)
                throw new IOException("Writing to the Host device failed (Windows error " + Marshal.GetLastWin32Error() + ").");
            return ((int)Sent);
        }

        /// <summary>Speed test only: one bulk IN transfer of up to <paramref name="Length"/> bytes into <paramref name="Data"/>; returns the number of bytes received.</summary>
        public int ReadRaw(byte[] Data, int Length)
        {
            uint Got;
            if (WinUsb_ReadPipe(WinUsbHandle, InPipe, Data, (uint)Length, out Got, IntPtr.Zero) == false)
                throw new IOException("Reading from the Host device failed (Windows error " + Marshal.GetLastWin32Error() + ").");
            return ((int)Got);
        }

        public void CancelRead()
        {
            ReadCanceled = true;
            if (Volatile.Read(ref DisposedFlag) == 0)
                WinUsb_AbortPipe(WinUsbHandle, InPipe);
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref DisposedFlag, 1) != 0)
                return;

            ReadCanceled = true;
            WinUsb_AbortPipe(WinUsbHandle, InPipe);
            WinUsb_AbortPipe(WinUsbHandle, OutPipe);
            WinUsb_Free(WinUsbHandle);
            DeviceHandle.Dispose();
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct SpDeviceInterfaceData
        {
            public uint cbSize;
            public Guid InterfaceClassGuid;
            public uint Flags;
            public IntPtr Reserved;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        private struct UsbInterfaceDescriptor
        {
            public byte bLength;
            public byte bDescriptorType;
            public byte bInterfaceNumber;
            public byte bAlternateSetting;
            public byte bNumEndpoints;
            public byte bInterfaceClass;
            public byte bInterfaceSubClass;
            public byte bInterfaceProtocol;
            public byte iInterface;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct WinUsbPipeInformation
        {
            public int PipeType;
            public byte PipeId;
            public ushort MaximumPacketSize;
            public byte Interval;
        }

        [DllImport("setupapi.dll", SetLastError = true)]
        private static extern IntPtr SetupDiGetClassDevs(ref Guid ClassGuid, IntPtr Enumerator, IntPtr ParentWindow, uint Flags);

        [DllImport("setupapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiEnumDeviceInterfaces(IntPtr DeviceInfoSet, IntPtr DeviceInfoData, ref Guid InterfaceClassGuid, uint MemberIndex, ref SpDeviceInterfaceData DeviceInterfaceData);

        [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode, EntryPoint = "SetupDiGetDeviceInterfaceDetailW")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr DeviceInfoSet, ref SpDeviceInterfaceData DeviceInterfaceData, IntPtr DeviceInterfaceDetailData, uint DeviceInterfaceDetailDataSize, out uint RequiredSize, IntPtr DeviceInfoData);

        [DllImport("setupapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern SafeFileHandle CreateFile(string FileName, uint DesiredAccess, uint ShareMode, IntPtr SecurityAttributes, uint CreationDisposition, uint FlagsAndAttributes, IntPtr TemplateFile);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_Initialize(SafeFileHandle DeviceHandle, out IntPtr InterfaceHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_Free(IntPtr InterfaceHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_QueryInterfaceSettings(IntPtr InterfaceHandle, byte AlternateSettingNumber, out UsbInterfaceDescriptor UsbAltInterfaceDescriptor);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_QueryPipe(IntPtr InterfaceHandle, byte AlternateInterfaceNumber, byte PipeIndex, out WinUsbPipeInformation PipeInformation);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_SetPipePolicy(IntPtr InterfaceHandle, byte PipeId, uint PolicyType, uint ValueLength, ref uint Value);

        [DllImport("winusb.dll", SetLastError = true, EntryPoint = "WinUsb_SetPipePolicy")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_SetPipePolicyByte(IntPtr InterfaceHandle, byte PipeId, uint PolicyType, uint ValueLength, ref byte Value);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_ReadPipe(IntPtr InterfaceHandle, byte PipeId, [Out] byte[] Buffer, uint BufferLength, out uint LengthTransferred, IntPtr Overlapped);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_WritePipe(IntPtr InterfaceHandle, byte PipeId, byte[] Buffer, uint BufferLength, out uint LengthTransferred, IntPtr Overlapped);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_AbortPipe(IntPtr InterfaceHandle, byte PipeId);

        [DllImport("winusb.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WinUsb_FlushPipe(IntPtr InterfaceHandle, byte PipeId);
    }
}
