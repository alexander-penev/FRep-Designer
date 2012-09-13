using System;

namespace FRepDesigner
{
    public class FRepSolid : Solid
    {
        /// <summary>
        /// The F-Rep expression.
        /// </summary>
        public string Expression;

        /// <summary>
        /// Initializes a new instance of the <see cref="FRepDesigner.FRepSolid"/> class.
        /// </summary>
        public FRepSolid()
        {
            Expression = "0";
        }

        /// <summary>
        /// Initializes a new instance of the <see cref="FRepDesigner.FRepSolid"/> class.
        /// </summary>
        /// <param name='expression'>
        /// Expression (F-Rep).
        /// </param>
        public FRepSolid(string expression)
        {
            this.Expression = expression;
        }

        public FRepSolid(FRepSolid solid): this (solid.Expression) {}

        /// <summary>
        /// Intersect the specified point p with solid.
        /// </summary>
        /// <param name='p'>
        /// Point.
        /// </param>
        public override bool Intersect(Point3D p)
        {
          return (Evaluator.Eval(Expression, p.X, p.Y, p.Z) <= 0);
        }

        /// <summary>
        /// Intersect the specified ray r with solid.
        /// </summary>
        /// <param name='r'>
        /// Ray.
        /// </param>
        public override Point3D Intersect(Ray3D r)
        {
          return null; //TODO: implement ray-solid intersection
        }

    }
}

