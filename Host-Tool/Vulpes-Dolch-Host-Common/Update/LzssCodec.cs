using System;
using System.Collections.Generic;

namespace VulpesDolch.Host.Common.Update
{
    /// <summary>
    /// The LZSS flavour the firmware decodes (UpdateCore.c): 4095 byte window, matches of 3-18 bytes, flags packed 8 per byte starting at
    /// the lowest bit (1 = a literal follows, 0 = a two byte match: offset low byte, then offset high nibble and length - 3).
    /// </summary>
    public static class LzssCodec
    {
        private const int Window = 4095;
        private const int MinimumMatch = 3;
        private const int MaximumMatch = 18;
        private const int MaximumChain = 256;
        private const int HashSize = 1 << 15;

        private static int Hash(byte[] Data, int Position)
        {
            return (((Data[Position] << 10) ^ (Data[Position + 1] << 5) ^ Data[Position + 2]) & (HashSize - 1));
        }

        /// <summary>
        /// Optimal parse: a literal costs 9 bits (flag + byte), a match 17 bits (flag + two bytes) whatever its length, so a dynamic program
        /// over the whole input finds the cheapest mix. A match found at a position can be shortened to any length from 3, with the same offset.
        /// The output is the format the firmware decodes; only the choice of matches differs from a greedy encoder.
        /// </summary>
        public static byte[] Compress(byte[] Data)
        {
            int Count = Data.Length;
            int[] MatchLength = new int[Count + 1];
            int[] MatchOffset = new int[Count + 1];
            int[] Head = new int[HashSize];
            int[] Previous = new int[Count + 1];

            for (int Index = 0; Index < HashSize; Index++)
                Head[Index] = -1;

            // For every position the longest match inside the window (any shorter length is possible as well).
            for (int Position = 0; Position < Count; Position++)
            {
                if (Position + MinimumMatch <= Count)
                {
                    int Candidate = Head[Hash(Data, Position)];
                    int Chain = MaximumChain;
                    int Limit = Math.Min(MaximumMatch, Count - Position);
                    int BestLength = 0;
                    int BestOffset = 0;
                    while ((Candidate >= 0) && (Position - Candidate <= Window) && (Chain > 0))
                    {
                        int Length = 0;
                        while ((Length < Limit) && (Data[Candidate + Length] == Data[Position + Length]))
                            Length++;
                        if (Length > BestLength)
                        {
                            BestLength = Length;
                            BestOffset = Position - Candidate;
                            if (BestLength >= Limit)
                                break;
                        }
                        Candidate = Previous[Candidate];
                        Chain--;
                    }
                    if (BestLength >= MinimumMatch)
                    {
                        MatchLength[Position] = BestLength;
                        MatchOffset[Position] = BestOffset;
                    }

                    int Key = Hash(Data, Position);
                    Previous[Position] = Head[Key];
                    Head[Key] = Position;
                }
            }

            // Cheapest way to code the first Index bytes, in bits; Choice remembers the last step (0 = literal, otherwise the match length).
            int[] Cost = new int[Count + 1];
            int[] Choice = new int[Count + 1];
            for (int Index = 1; Index <= Count; Index++)
                Cost[Index] = int.MaxValue;
            for (int Position = 0; Position < Count; Position++)
            {
                if (Cost[Position + 1] > Cost[Position] + 9)
                {
                    Cost[Position + 1] = Cost[Position] + 9;
                    Choice[Position + 1] = 0;
                }
                for (int Length = MinimumMatch; Length <= MatchLength[Position]; Length++)
                {
                    if (Cost[Position + Length] > Cost[Position] + 17)
                    {
                        Cost[Position + Length] = Cost[Position] + 17;
                        Choice[Position + Length] = Length;
                    }
                }
            }

            // Walk back to get the steps in order.
            List<int> Steps = new List<int>();
            for (int Index = Count; Index > 0;)
            {
                int Length = (Choice[Index] == 0) ? 1 : Choice[Index];
                Steps.Add(Choice[Index]);
                Index -= Length;
            }
            Steps.Reverse();

            List<byte> Output = new List<byte>(Count / 2 + 16);
            int FlagIndex = -1;
            int FlagBit = 8;
            int Source = 0;
            foreach (int Step in Steps)
            {
                if (FlagBit == 8)
                {
                    FlagIndex = Output.Count;
                    Output.Add(0);
                    FlagBit = 0;
                }

                if (Step == 0)
                {
                    Output[FlagIndex] = (byte)(Output[FlagIndex] | (1 << FlagBit));
                    Output.Add(Data[Source]);
                    Source++;
                }
                else
                {
                    int Offset = MatchOffset[Source];
                    Output.Add((byte)(Offset & 0xFF));
                    Output.Add((byte)(((Offset >> 8) << 4) | (Step - MinimumMatch)));
                    Source += Step;
                }
                FlagBit++;
            }

            return (Output.ToArray());
        }

        /// <summary>Returns null if the stream is damaged or ends early.</summary>
        public static byte[] Decompress(byte[] Source, int OutputLength)
        {
            byte[] Output = new byte[OutputLength];
            int Produced = 0;
            int SourcePosition = 0;

            while (Produced < OutputLength)
            {
                if (SourcePosition >= Source.Length)
                    return (null);

                int Flags = Source[SourcePosition++];
                for (int Bit = 0; (Bit < 8) && (Produced < OutputLength); Bit++)
                {
                    if ((Flags & (1 << Bit)) != 0)
                    {
                        if (SourcePosition >= Source.Length)
                            return (null);
                        Output[Produced++] = Source[SourcePosition++];
                    }
                    else
                    {
                        if (SourcePosition + 1 >= Source.Length)
                            return (null);
                        int First = Source[SourcePosition++];
                        int Second = Source[SourcePosition++];
                        int Offset = First | ((Second >> 4) << 8);
                        int Length = (Second & 0x0F) + MinimumMatch;
                        if ((Offset == 0) || (Offset > Produced))
                            return (null);
                        for (int Copy = 0; (Copy < Length) && (Produced < OutputLength); Copy++)
                        {
                            Output[Produced] = Output[Produced - Offset];
                            Produced++;
                        }
                    }
                }
            }

            return (Output);
        }
    }
}
