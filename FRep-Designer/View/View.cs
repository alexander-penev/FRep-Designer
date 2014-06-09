using System;
using System.Drawing;

using Cairo;

namespace FRepDesigner
{
    public class View
    {
        public View()
        {
        }
        
        public virtual Cairo.Color Тrace(Scene model, Ray3D r)
        {
            return new Cairo.Color(0,0,0,0);
        }
        
        public virtual Cairo.Color Тrace(Scene model, Ray3D r, out Solid sld)
        {
            sld = null;
            return new Cairo.Color(0,0,0,0);
        }
        
        
        public virtual void Render(Scene model, Gdk.Pixmap pixmap)
        {
        }
    }
}

