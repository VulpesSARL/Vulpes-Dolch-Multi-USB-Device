namespace VulpesDolch.Host.Common.Protocol
{
    /// <summary>One message: what travels between the PC, the Host device and the Client device, cut into link reports on the way.</summary>
    public sealed class Message
    {
        public MessageChannel Channel { get; }

        /// <summary>Numbered per channel.</summary>
        public byte Type { get; }

        /// <summary>Chosen by the requester; answers carry the tag of the request they belong to.</summary>
        public ushort Tag { get; }

        /// <summary>Exactly the payload bytes (the padding to 32 bit words is added and removed by the codec).</summary>
        public byte[] Payload { get; }

        /// <summary>Stopwatch timestamp of the moment a function queued the message (diagnostics only, not part of the protocol).</summary>
        internal long ArrivedAt { get; set; }

        public Message(MessageChannel Channel, byte Type, ushort Tag, byte[] Payload)
        {
            this.Channel = Channel;
            this.Type = Type;
            this.Tag = Tag;
            this.Payload = (Payload != null) ? Payload : new byte[0];
        }
    }
}
