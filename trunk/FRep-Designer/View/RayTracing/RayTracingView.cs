using System;
using System.Drawing;

using Cairo;

namespace FRepDesigner
{
	public class RayTracingView
	{
        public RayTracingView(): base ()
        {
        }
        
        public virtual void Render(Scene model, Gdk.Pixmap pixmap)
        {
        }
	}

}
