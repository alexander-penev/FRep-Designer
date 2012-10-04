using System;
using System.Collections.Generic;

namespace FRepDesigner
{
    public class Scene
    {
        public List<Solid> Solids = new List<Solid>();

        public Scene()
        {
        }
        public void AddPrimitive(FRepSolid primitive)
        {
            this.Solids.Add(primitive);
        }
    }
}

