using System;

namespace FRepDesigner
{
    public abstract class Solid
    {
        public Solid()
        {
           
        }

        // prinadlejnist na tochka (sechenie s tochka)
        public virtual bool Intersect(Point3D p)
            {
                return false;
            }

        //ray with silod
        public virtual Point3D Intersect(Ray3D p)
            {
                return null;
            }


    }
}

