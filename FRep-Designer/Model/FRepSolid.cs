using System;

using System.Reflection;

namespace FRepDesigner
{
    public class FRepSolid : Solid
    {
        public const float eps = 1e-3f;

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
        /// Ray. Ray direction must be unit vector (|r.Direction| = 1).
        /// </param>
        public override Point3D Intersect(Ray3D r)
        {
          // Brute force ray-solid intersection
          Point3D p = new Point3D(r.Start);
          MethodInfo mi;
          float f;
          float ff = 0;
          
          int sgn = Math.Sign(Evaluator.Eval(Expression, out mi, p.X, p.Y, p.Z));
          
          while (true) {
            f = (float)mi.Invoke(null, new object[3]{p.X, p.Y, p.Z});
            
            if (Math.Sign(f) != sgn) return p;
            f = Math.Abs(f);
            ff += f;
            if (ff > 100) return null;
            if (f < eps) f = eps;

            p.X += r.Direction.X * f;
            p.Y += r.Direction.Y * f;
            p.Z += r.Direction.Z * f;
          }
        }

        // normal vector in surface point
        public override Vector3D Normal(Point3D p)
        {
            MethodInfo mi;
            float f = Evaluator.Eval(Expression, out mi, p.X, p.Y, p.Z);
            float fx = (float)mi.Invoke(null, new object[3]{p.X+eps, p.Y, p.Z});
            float fy = (float)mi.Invoke(null, new object[3]{p.X, p.Y+eps, p.Z});
            float fz = (float)mi.Invoke(null, new object[3]{p.X, p.Y, p.Z+eps});
            Vector3D N = new Vector3D((fx-f)/eps, (fy-f)/eps, (fz-f)/eps);
            N.Normalize();
            return N;
        }

    }
}

