using System;
using System.Collections.Generic;

using Microsoft.CSharp;
using System.CodeDom.Compiler;
using System.Reflection;

namespace FRepDesigner
{
    public static class Evaluator
    {
        private static Dictionary<string, MethodInfo> cachedEvaluators = new Dictionary<string, MethodInfo>();
        private static string[] referenceAssemblies = {"System"};
        private static string usingClauses = ""; 
        private static string embededFunctions = 
            "public const double pi = Math.PI;"+
            "public const double e = Math.E;"+
            "public static float abs(float v) { return Math.Abs(v); }"+
            "public static float acos(float v) { return (float)Math.Acos(v); }"+
            "public static float asin(float v) { return (float)Math.Asin(v); }"+
            "public static float atan(float v) { return (float)Math.Atan(v); }"+
            "public static float atan2(float v1, float v2) { return (float)Math.Atan2(v1,v2); }"+
            "public static float ceiling(float v) { return (float)Math.Ceiling(v); }"+
            "public static float cosh(float v) { return (float)Math.Cosh(v); }"+
            "public static float exp(float v) { return (float)Math.Exp(v); }"+
            "public static float floor(float v) { return (float)Math.Floor(v); }"+
            "public static float log(float v) { return (float)Math.Log(v); }"+
            "public static float log(float v1, float v2) { return (float)Math.Log(v1,v2); }"+
            "public static float log10(float v) { return (float)Math.Log10(v); }"+
            "public static float max(float v1, float v2) { return Math.Max(v1,v2); }"+
            "public static float min(float v1, float v2) { return Math.Min(v1,v2); }"+
            "public static float pow(float v1, float v2) { return (float)Math.Pow(v1,v2); }"+
            "public static float round(float v) { return (float)Math.Round(v); }"+
            "public static float round(float v, int d) { return (float)Math.Round(v,d); }"+
            "public static int sign(float v) { return Math.Sign(v); }"+
            "public static float sin(float v) { return (float)Math.Sin(v); }"+
            "public static float sinh(float v) { return (float)Math.Sinh(v); }"+
            "public static float sqrt(float v) { return (float)Math.Sqrt(v); }"+
            "public static float tan(float v) { return (float)Math.Tan(v); }"+
            "public static float tanh(float v) { return (float)Math.Tanh(v); }"+
            "public static float truncate(float v) { return (float)Math.Truncate(v); }";
    
        static Evaluator()
        {
        }

        public static float Eval(string expression, params object[] p)
        {
            MethodInfo mi;

            if (!cachedEvaluators.TryGetValue(expression, out mi)) {
                string[] code = {
                    string.Format("using System; {0} public static class _Eval {{ {1} public static float _M(float x, float y, float z) {{ return ({2}); }} }}", usingClauses, embededFunctions, expression) 
                };

                //CompilerParameters compilerParams = new CompilerParameters(referenceAssemblies, "Test.dll");
                CompilerParameters compilerParams = new CompilerParameters(referenceAssemblies);
                compilerParams.GenerateExecutable = false;
                compilerParams.GenerateInMemory = true;
                compilerParams.IncludeDebugInformation = false;
                CSharpCodeProvider compiler = (CSharpCodeProvider)CodeDomProvider.CreateProvider("CSharp");

                CompilerResults result = compiler.CompileAssemblyFromSource(compilerParams, code);

                if (result.Errors.HasErrors) throw new Exception(result.Errors[0].ErrorText);

                Type evalType = result.CompiledAssembly.GetType("_Eval");
                mi = evalType.GetMethod("_M");
            }

            return (float)mi.Invoke(null, p);
        }
    }
}

