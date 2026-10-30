using System;
using System.Collections.Concurrent;
using System.Threading;
using VulpesDolch.Host.Common.Service;

namespace Vulpes_Dolch_Host_Tool
{
    /// <summary>
    /// Sends keyboard and mouse events to the service in the order they happened, on a thread of its own, so that the keyboard hook and the window thread never wait
    /// for the pipe. Nothing is coalesced here (a key transition must never get lost); the mouse movement is already summed up by the window.
    /// </summary>
    internal sealed class InputSender : IDisposable
    {
        private const int MaxQueued = 4000;

        private readonly ServiceClient Client;
        private readonly BlockingCollection<ServiceRequest> Queue = new BlockingCollection<ServiceRequest>();
        private readonly Thread Worker;

        public InputSender(ServiceClient Client)
        {
            this.Client = Client;
            Worker = new Thread(WorkerMain);
            Worker.Name = "Vulpes-Dolch input sender";
            Worker.IsBackground = true;
            Worker.Start();
        }

        public void KeyDown(byte Usage)
        {
            Post(Make(ServiceCommand.KeyDown, Usage));
        }

        public void KeyUp(byte Usage)
        {
            Post(Make(ServiceCommand.KeyUp, Usage));
        }

        public void MouseMove(int Dx, int Dy, int Wheel)
        {
            ServiceRequest Request = Make(ServiceCommand.MouseMove, 0);
            Request.Dx = Dx;
            Request.Dy = Dy;
            Request.Wheel = Wheel;
            Post(Request);
        }

        public void MouseButton(int Button, bool Down)
        {
            ServiceRequest Request = Make(ServiceCommand.MouseButton, Button);
            Request.Flag = Down;
            Post(Request);
        }

        public void ReleaseAll()
        {
            Post(Make(ServiceCommand.ReleaseAll, 0));
        }

        private static ServiceRequest Make(ServiceCommand Command, int Value)
        {
            ServiceRequest Request = new ServiceRequest();
            Request.Command = Command;
            Request.Value = Value;
            return (Request);
        }

        private void Post(ServiceRequest Request)
        {
            if (Queue.IsAddingCompleted == true)
                return;

            // If the service stops answering, do not pile up movement forever; key events always go in.
            if ((Queue.Count > MaxQueued) && (Request.Command == ServiceCommand.MouseMove))
                return;

            try
            {
                Queue.Add(Request);
            }
            catch (InvalidOperationException)
            {
                // Disposed meanwhile.
            }
        }

        private void WorkerMain()
        {
            foreach (ServiceRequest Request in Queue.GetConsumingEnumerable())
            {
                try
                {
                    Client.Call(Request, 2000);
                }
                catch (Exception)
                {
                    // A failed call is lost like a dropped packet; the next one goes on.
                }
            }
        }

        public void Dispose()
        {
            Queue.CompleteAdding();
        }
    }
}
