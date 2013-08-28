using System;
using Cairo;

namespace FRepDesigner
{
    public abstract class Solid
    {
        public Cairo.Color Color = new Cairo.Color(1,1,1,1);

        public Solid()
        {      
        }

        // Intersect point with solid
        public virtual bool Intersect(Point3D p)
        {
            return false;
        }

        // Intersect ray with solid
        public virtual Point3D Intersect(Ray3D r)
        {
            return null;
        }

        // Normal vector in surface point
        public virtual Vector3D Normal(Point3D p)
        {
            return null;
        }
        

    }
}

