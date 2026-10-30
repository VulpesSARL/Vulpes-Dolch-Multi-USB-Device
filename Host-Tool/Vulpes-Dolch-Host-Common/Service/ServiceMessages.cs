using System;

namespace VulpesDolch.Host.Common.Service
{
    /// <summary>What a GUI (or any other client) can ask the service to do.</summary>
    public enum ServiceCommand
    {
        GetState = 0,
        InsertCd = 1,
        EjectCd = 2,
        AttachDisk = 3,
        DetachDisk = 4,
        KeyDown = 5,
        KeyUp = 6,
        MouseMove = 7,
        MouseButton = 8,
        ReleaseAll = 9,
    }

    /// <summary>A request from a client to the service. Only the fields the command needs are used.</summary>
    public sealed class ServiceRequest
    {
        public ServiceCommand Command { get; set; }

        /// <summary>Image path (InsertCd, AttachDisk).</summary>
        public string Path { get; set; }

        /// <summary>EjectCd / DetachDisk: force; AttachDisk: read-only; MouseButton: pressed.</summary>
        public bool Flag { get; set; }

        /// <summary>KeyDown / KeyUp: the HID usage; MouseButton: the button mask (1 left, 2 right, 4 middle).</summary>
        public int Value { get; set; }

        /// <summary>MouseMove: relative movement and wheel notches.</summary>
        public int Dx { get; set; }

        public int Dy { get; set; }

        public int Wheel { get; set; }

        /// <summary>InsertCd / AttachDisk of an image on a network share: the account for the share (DOMAIN\user or user@domain). Empty = the service's own (machine) account.</summary>
        public string User { get; set; }

        /// <summary>The password for <see cref="User"/>. Only sent together with a load command; the service keeps it in memory only while it connects the share.</summary>
        public string Password { get; set; }
    }

    public sealed class ServiceResponse
    {
        public bool Ok { get; set; }

        /// <summary>The command failed because the image file could not be opened (missing, unreadable, not an image); a client may drop it from its history.</summary>
        public bool FileError { get; set; }

        /// <summary>The image is on a network share the service's account may not use (or the given credentials were wrong): ask the user for credentials and send the command again with User and Password.</summary>
        public bool NeedsCredentials { get; set; }

        /// <summary>The share (\\server\share) <see cref="NeedsCredentials"/> is about.</summary>
        public string Share { get; set; }

        /// <summary>Why the command failed (empty when it worked).</summary>
        public string Error { get; set; }
    }

    /// <summary>What the service tells every connected client; sent after every change and whenever a client asks.</summary>
    public sealed class ServiceState
    {
        /// <summary>The service has a session with the Host device. All controls of an application are disabled while this is false.</summary>
        public bool DeviceConnected { get; set; }

        /// <summary>"1.0.2610.2902", "none" or empty when unknown.</summary>
        public string HostFirmware { get; set; }

        public string ClientFirmware { get; set; }

        /// <summary>0 unknown / not connected, 1 full speed, 2 high speed, 3 SuperSpeed (USB speed of the Host device towards its PC).</summary>
        public int HostUsbSpeed { get; set; }

        /// <summary>0 unknown, 1 full speed, 2 high speed, 3 SuperSpeed; -1 = the Client's USB side is disconnected.</summary>
        public int ClientUsbSpeed { get; set; }

        /// <summary>"Running", "Booting", "Lost", "Absent".</summary>
        public string ClientState { get; set; }

        public string CdPath { get; set; }

        public bool CdPresent { get; set; }

        public bool CdLocked { get; set; }

        public string DiskPath { get; set; }

        public bool DiskPresent { get; set; }

        public bool DiskReadOnly { get; set; }

        /// <summary>The keyboard LEDs of the target PC: bit 0 Num Lock, bit 1 Caps Lock, bit 2 Scroll Lock; -1 = not known yet. A GUI that has captured the keyboard follows these.</summary>
        public int TargetLeds { get; set; }

        /// <summary>Text for a status line, e.g. the last problem the service had.</summary>
        public string Message { get; set; }
    }

    /// <summary>The one frame type on the pipe (length prefixed JSON).</summary>
    public sealed class ServiceFrame
    {
        /// <summary>Request frames and their responses carry the same Id; pushed state frames use 0.</summary>
        public int Id { get; set; }

        public ServiceRequest Request { get; set; }

        public ServiceResponse Response { get; set; }

        public ServiceState State { get; set; }
    }

    public static class ServiceNames
    {
        public const string PipeName = "VulpesDolchHostService";
        public const string ServiceName = "VulpesDolchHost";
        public const string DisplayName = "Vulpes Dolch Host Service";
    }
}
