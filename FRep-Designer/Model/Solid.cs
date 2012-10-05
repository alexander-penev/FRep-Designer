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

        // prinadlejnist na tochka (sechenie s tochka)
        public virtual bool Intersect(Point3D p)
        {
            return false;
        }

        // ray with silod
        public virtual Point3D Intersect(Ray3D r)
        {
            return null;
        }

        // normal vector in surface point
        public virtual Vector3D Normal(Point3D p)
        {
            return null;
        }
        

    }
}

