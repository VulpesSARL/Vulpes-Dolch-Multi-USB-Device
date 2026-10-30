using System;
using System.Globalization;
using System.IO;
using System.Text;

namespace VulpesDolch.Stamp
{
    /// <summary>
    /// Writes the build stamp "Major.Minor.YYMM.DDXX" (XX = build number of the day, 01..99) into FirmwareVersion.h for the Host / Client firmware and,
    /// optionally, into a C# file for the PC library. The "Major.Minor" prefix is read from Version.txt (hard coded in that file).
    /// The build number is kept in the first line of the generated header (like the old AutoVersion tool): it counts up during a day and starts at 1 on a new day.
    /// Run it once before building Host and Client, so that both carry the same ID.
    ///
    /// Vulpes-Dolch-Stamp &lt;VersionFolder&gt; [--cs &lt;BuildVersion.cs&gt;]
    /// </summary>
    internal static class Program
    {
        private const string HeaderName = "FirmwareVersion.h";
        private const string PrefixName = "Version.txt";

        private static int Main(string[] Args)
        {
            if (Args.Length < 1)
            {
                Console.Error.WriteLine("Usage: Vulpes-Dolch-Stamp <VersionFolder> [--cs <BuildVersion.cs>]");
                return (2);
            }

            string Folder = Args[0];
            string CsFile = null;
            if ((Args.Length >= 3) && (Args[1] == "--cs"))
                CsFile = Args[2];

            string PrefixFile = Path.Combine(Folder, PrefixName);
            if (File.Exists(PrefixFile) == false)
            {
                Console.Error.WriteLine("Missing " + PrefixFile + " (one line, e.g. 1.0).");
                return (1);
            }

            string[] PrefixParts = File.ReadAllLines(PrefixFile)[0].Trim().Split('.');
            int Major;
            int Minor;
            if ((PrefixParts.Length != 2) || (int.TryParse(PrefixParts[0], out Major) == false) || (int.TryParse(PrefixParts[1], out Minor) == false) ||
                (Major < 0) || (Major > 15) || (Minor < 0) || (Minor > 15))
            {
                Console.Error.WriteLine(PrefixFile + " must contain Major.Minor with both numbers 0..15.");
                return (1);
            }

            DateTime Now = DateTime.Now;
            string Today = Now.ToString("yyMMdd", CultureInfo.InvariantCulture);
            int Build = 1;

            string HeaderFile = Path.Combine(Folder, HeaderName);
            if (File.Exists(HeaderFile) == true)
            {
                string First = File.ReadAllLines(HeaderFile)[0];
                string[] Fields = First.Replace("/*", "").Replace("*/", "").Trim().Split(':');
                int Previous;
                if ((Fields.Length >= 3) && (Fields[0].Trim() == "REV") && (int.TryParse(Fields[1], out Previous) == true) && (Fields[2].Trim() == Today))
                    Build = Previous + 1;
            }

            if (Build > 99)
            {
                Console.Error.WriteLine("More than 99 builds today - the build number has two digits.");
                return (1);
            }

            int Year = Now.Year % 100;
            uint Packed = ((uint)Major << 27) | ((uint)Minor << 23) | ((uint)Year << 16) | ((uint)Now.Month << 12) | ((uint)Now.Day << 7) | (uint)Build;
            string Text = Major + "." + Minor + "." + Now.ToString("yyMM", CultureInfo.InvariantCulture) + "." + Now.ToString("dd", CultureInfo.InvariantCulture) + Build.ToString("00", CultureInfo.InvariantCulture);

            // Layout of the packed number (bit 31 stays 0): Major 30..27, Minor 26..23, YY 22..16, MM 15..12, DD 11..7, XX 6..0.
            string Header = "/* REV:" + Build + ":" + Today + ": written by Vulpes-Dolch-Stamp, do not edit by hand */\r\n" +
                "#ifndef FIRMWARE_VERSION_H\r\n" +
                "#define FIRMWARE_VERSION_H\r\n" +
                "\r\n" +
                "#define FW_VERSION_STRING                \"" + Text + "\"\r\n" +
                "#define FW_VERSION_PACKED                (0x" + Packed.ToString("X8", CultureInfo.InvariantCulture) + "u)\r\n" +
                "\r\n" +
                "#endif\r\n";

            try
            {
                File.WriteAllText(HeaderFile, Header, new UTF8Encoding(false));
                if (CsFile != null)
                {
                    string Cs = "namespace VulpesDolch.Host.Common\r\n" +
                        "{\r\n" +
                        "    /// <summary>Written by Vulpes-Dolch-Stamp; the stamp of the last firmware / library build set.</summary>\r\n" +
                        "    public static class BuildVersion\r\n" +
                        "    {\r\n" +
                        "        public const string Text = \"" + Text + "\";\r\n" +
                        "        public const uint Packed = 0x" + Packed.ToString("X8", CultureInfo.InvariantCulture) + ";\r\n" +
                        "    }\r\n" +
                        "}\r\n";
                    File.WriteAllText(CsFile, Cs, new UTF8Encoding(false));
                }
            }
            catch (IOException Problem)
            {
                Console.Error.WriteLine(Problem.Message);
                return (1);
            }

            Console.WriteLine("VERSION: " + Text + " (0x" + Packed.ToString("X8", CultureInfo.InvariantCulture) + ")");
            return (0);
        }
    }
}
