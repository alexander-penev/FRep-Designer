using System;
using NUnit.Framework;

namespace FRepDesigner
{
    [TestFixture]
    public class TestFRepSolid
    {
        [Test]
        public void PointInSphere()
        {
            FRepSolid solid = new FRepSolid("x*x + y*y + z*z - 1");
            Assert.AreEqual(true, solid.Intersect(new Point3D(0,0,0)));
        }

        [Test]
        public void PointOutSphere()
        {
            FRepSolid solid = new FRepSolid("x*x + y*y + z*z - 1");
            Assert.AreEqual(false, solid.Intersect(new Point3D(10,0,0)));
        }

        [Test]
        public void RaySphereIntersectionPositive()
        {
            FRepSolid solid = new FRepSolid("x*x + y*y + z*z - 1");
            Assert.AreNotEqual(null, solid.Intersect(new Ray3D(new Vector3D(0,0,1), new Point3D(0,0,-2))));
        }

        [Test]
        public void RaySphereIntersectionNegative()
        {
            FRepSolid solid = new FRepSolid("x*x + y*y + z*z - 1");
            Assert.AreEqual(null, solid.Intersect(new Ray3D(new Vector3D(0,0,1), new Point3D(0,0,2))));
        }

        [Test]
        public void SphereNormal()
        {
            FRepSolid solid = new FRepSolid("x*x + y*y + z*z - 1");
            Vector3D N = solid.Normal(new Point3D(1,0,0));
            Assert.AreEqual(1, N.X, 1e-3);
            Assert.AreEqual(0, N.Y, 1e-3);
            Assert.AreEqual(0, N.Z, 1e-3);
        }
    }
}

