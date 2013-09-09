using System;
using System.Drawing;
using System.Runtime.InteropServices;

using Cairo;

namespace FRepDesigner
{
	public class OpenCLRayTracingView: RayTracingView
	{
		public OpenCLRayTracingView(): base()
		{
			init();
		}

		~OpenCLRayTracingView()
		{
			//destroy();
		}

		public override void Render(Scene model, Gdk.Pixmap pixmap)
		{

			int width, height;
			pixmap.GetSize(out width, out height);
			byte[] image = new byte[4 * width * height];
			IntPtr p = Marshal.UnsafeAddrOfPinnedArrayElement (image, 0);
			getImage (p, width, height);

			pixmap.DrawRgb32Image (new Gdk.GC(pixmap), 0, 0, width, height, Gdk.RgbDither.None, image,  width*4); 

		}

		[DllImport ("frep-cl-tracer")]
        private static extern byte[] getImage(IntPtr p, int width, int height);

		[DllImport ("frep-cl-tracer")]
        private static extern void trace();

		[DllImport ("frep-cl-tracer")]
        private static extern void init();

		[DllImport ("frep-cl-tracer")]
        private static extern void destroy();
	}
}
