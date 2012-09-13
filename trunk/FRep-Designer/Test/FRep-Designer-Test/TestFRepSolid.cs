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
    }
}

