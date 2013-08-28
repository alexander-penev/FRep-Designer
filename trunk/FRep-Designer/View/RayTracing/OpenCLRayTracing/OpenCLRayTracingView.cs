using System;
using System.Drawing;

using Cairo;

namespace FRepDesigner
{
    public class OpenCLRayTracingView: RayTracingView
    {
        public OpenCLRayTracingView(): base ()
        {
        }

        public override void Render(Scene model, Gdk.Pixmap pixmap)
        {
            int width, height;
            pixmap.GetSize(out width, out height);
            // TODO: Render
        }
    }
}
