namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>What the Host device knows about the Client device (STATUS message).</summary>
    public enum ClientState : byte
    {
        Absent = 0,

        /// <summary>The Client was reset and its application is starting.</summary>
        Booting = 1,

        Running = 2,
        Lost = 3
    }
}
