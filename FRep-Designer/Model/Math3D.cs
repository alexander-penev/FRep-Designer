using System;

namespace FRepDesigner
{   
    public class Vector3D
    {
        public float X;
        public float Y;
        public float Z;

        //bezkrajna tochka i null
        public static Vector3D Infinity = new Vector3D(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);  
        public static Vector3D Zero = new Vector3D();

        public Vector3D()
        {
        }

        public Vector3D(Vector3D v)
        {
            this.X = v.X;
            this.Y = v.Y;
            this.Z = v.Z;
        }

        public Vector3D(float x, float y, float z)
        {
            this.X = x;
            this.Y = y;
            this.Z = z;
        }
    }
    public class Point3D : Vector3D
    {
        public Point3D(): base () {}
        public Point3D(Vector3D v): base (v) {}
        public Point3D(float x, float y, float z): base (x,y,z) {}
    }

       //TODO:rorate
    //TODO:translate
    //TODO:suma na vectori (prenapisani operatori)
    //TODO:skalarni proizvedeniq
    //TODO:drugi metodi na vectori

     public class Ray3D
    {
        public Vector3D Direction;
        public Point3D Start;

        public Ray3D()
        {

        }
        public Ray3D(Vector3D direction, Point3D start)        
        {
            this.Direction = direction;
            this.Start = start;
        }
        // vika gorniq konstruktor v syshtiq klas
        public Ray3D (Ray3D ray): this (ray.Direction, ray.Start)
        {
        }
    }
}

