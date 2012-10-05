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

        public virtual void Render(Scene model, Gdk.Pixmap pixmap)
        {
        }
    }
}

