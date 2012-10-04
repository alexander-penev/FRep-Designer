using System;
using System.Drawing;

namespace FRepDesigner
{
    public class View
    {
        public Scene Model;
        public Gdk.Pixbuf Target; 
            
        public View()
        {
        }

        public View(Scene model, Gdk.Pixbuf target)
        {
            this.Model = model;
            this.Target = target;
        }

        public virtual void Render()
        {
        }
    }
}

