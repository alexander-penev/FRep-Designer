using System;
using System.Drawing;
using System.Threading.Tasks;

using Cairo;

namespace FRepDesigner
{
    //TODO: Fix multithreading performance problems
    public class SimpleMultithreadRayTracingView: RayTracingView
    {
        public static Point3D LightPos = new Point3D(10,10,20);
        public static float AmbientLightIntensity = 0.1f;
        public static float LightIntensity = 0.8f;
        
        object lockObject = new object();
        
        public SimpleMultithreadRayTracingView(): base ()
        {
        }
        
        public override Cairo.Color Тrace(Scene model, Ray3D r)
        {
            Point3D p, p1;
            float d1, d2;
            Solid sld;
            
            sld = null;
            p1 = new Point3D(r.Start);
            d1 = float.PositiveInfinity;
            foreach (Solid s in model.Solids) {
                p = s.Intersect(r);
                if (p != null) {
                    d2 = (p.X - p1.X) * (p.X - p1.X) + (p.Y - p1.Y) * (p.Y - p1.Y) + (p.Z - p1.Z) * (p.Z - p1.Z);
                    if (d2 < d1) {
                        p1 = p;
                        d1 = d2;
                        sld = s;
                    }
                }
            }
            if (sld != null) {
                Vector3D L = p1 - LightPos;
                L.Normalize();
                Vector3D N = sld.Normal(p1);
                float k = (float)(L * N);
                if (k > 0)
                    return new Cairo.Color(AmbientLightIntensity + sld.Color.R * k * LightIntensity, AmbientLightIntensity + sld.Color.G * k * LightIntensity, AmbientLightIntensity + sld.Color.B * k * LightIntensity, sld.Color.A);
                else
                    return new Cairo.Color(0, 0, 0, 1);
            }
            else {
                return new Cairo.Color(0, 0, 0, 1);
            }
            
        }
        
        public override void Render(Scene model, Gdk.Pixmap pixmap)
        {
            int width, height;
            pixmap.GetSize(out width, out height);
            
            using (Cairo.Context cr = Gdk.CairoHelper.Create(pixmap)) {
                Parallel.For(-height/2, height/2, y => {
                    for (int x = -width/2; x <= width/2; x++) {
                        Ray3D r = new Ray3D(new Vector3D(0, 0, 1), new Point3D((float)x / 10.0f, (float)y / 10.0f, 5.0f));
                        
                        Cairo.Color c = Тrace(model, r);
                        
                        lock (lockObject) {
                            cr.Color = c;
                            cr.MoveTo(x + width / 2, y + height / 2);
                            cr.LineTo(x + width / 2 + 1, y + height / 2);
                            cr.Stroke();
                        };
                    }
                });
            }
        }
    }
}
