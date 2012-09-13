using System;

namespace FRepDesigner
{
    public class FRepSolid : Solid
    {
        public string Expression;

        public FRepSolid()
        {
            Expression = "0";
        }

        public FRepSolid(string expression)
        {
            this.Expression = expression;
        }

        public FRepSolid(FRepSolid solid): this (solid.Expression) {}

        public override bool Intersect(Point3D p)
        {
          return false; //TODO: implement expression evaluation (eval(expression) <= 0)
        }

        //ray with silod
        public override Point3D Intersect(Ray3D p)
        {
          return null; //TODO: implement ray-solid intersection
        }

    }
}

