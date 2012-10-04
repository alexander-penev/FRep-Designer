using System;
using System.Drawing;

namespace FRepDesigner
{
    public class RayTracingView: View
    {
        public RayTracingView(): base ()
        {
        }

        public RayTracingView(Scene model, Gdk.Pixbuf target): base (model, target)
        {
        }

        public override void Render()
        {
            //Target..Clear(Color.White);
            //...
        }
    }
}

