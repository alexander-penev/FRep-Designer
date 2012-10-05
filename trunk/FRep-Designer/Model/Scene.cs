using System;
using System.Collections.Generic;

namespace FRepDesigner
{
    public delegate void ChangedEventHandler(Scene scene, EventArgs e);

    public class Scene
    {
        public List<Solid> Solids = new List<Solid>();

        public event ChangedEventHandler Changed;

        public Scene()
        {
        }

        public virtual void OnChanged(EventArgs e) 
        {
            if (Changed != null) Changed(this, e);
        }

        public void AddPrimitive(FRepSolid primitive)
        {
            this.Solids.Add(primitive);
            OnChanged(EventArgs.Empty);
        }

    }
}

