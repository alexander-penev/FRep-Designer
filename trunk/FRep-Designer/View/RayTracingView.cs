using System;
using System.Drawing;

using Cairo;

namespace FRepDesigner
{
    public class RayTracingView: View
    {
        public Point3D LightPos = new Point3D(10,10,10);
        public float AmbientLightIntensity = 0.1f;
        public float LightIntensity = 0.8f;

        public RayTracingView(): base ()
        {
        }

        public override void Render(Scene model, Gdk.Pixmap pixmap)
        {
            int width, height;
            pixmap.GetSize(out width, out height);
            Ray3D r = new Ray3D(new Vector3D(0,0,1), new Point3D(0,0,0));
            Point3D p, p1;
            float d1, d2;
            Solid sld;

            using (Cairo.Context cr = Gdk.CairoHelper.Create(pixmap)) {
                for (int y = -height/2; y <= height/2; y++)
                    for (int x = -width/2; x <= width/2; x++) {
                        r.Start.X = (float)x/10;
                        r.Start.Y = (float)y/10;
                        r.Start.Z = 5f;

                        sld = null;
                        p1 = new Point3D(r.Start);
                        d1 = float.PositiveInfinity;
                        
                        foreach (Solid s in model.Solids) {
                            p = s.Intersect(r);
                            if (p != null) {
                                d2 = (p.X-p1.X)*(p.X-p1.X)+(p.Y-p1.Y)*(p.Y-p1.Y)+(p.Z-p1.Z)*(p.Z-p1.Z);
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
                            float k = (float)(L*N);
                            if (k > 0)
                              cr.Color = new Cairo.Color(
                                AmbientLightIntensity + sld.Color.R * k * LightIntensity,
                                AmbientLightIntensity + sld.Color.G * k * LightIntensity,
                                AmbientLightIntensity + sld.Color.B * k * LightIntensity,
                                sld.Color.A);
                            else cr.Color = new Cairo.Color(0,0,0,1);
                        } else {
                            cr.Color = new Cairo.Color(0,0,0,1);
                        }
                        
                        cr.MoveTo(x+width/2, y+height/2);
                        cr.LineTo(x+width/2+1, y+height/2);
                        cr.Stroke();
                    }
                //c += 0.2;
                
                //cr.Rectangle(0, 0, width, height);
                //cr.SetSourceRGB(1, 1, 1);
                //cr.Fill();
            }
        }
    }
}
