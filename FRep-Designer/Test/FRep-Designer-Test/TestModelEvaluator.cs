using System;
using NUnit.Framework;

namespace FRepDesigner
{
    [TestFixture]
    public class TestModelEvaluator
    {
        [Test]
        public void SimpleExpression()
        {
            float x = 1;
            float y = 2;
            float z = 3;
            Assert.AreEqual(6, Evaluator.Eval("x+y+z", x,y,z));
        }

        [Test]
        public void EmbeddedFunctions()
        {
            float x = -1;
            float y = 2;
            float z = 3;
            Assert.AreEqual(6, Evaluator.Eval("abs(x)+y+z", x,y,z));
        }
    }
}

