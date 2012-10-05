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

        public float Length()
        {
            return (float)Math.Sqrt(this*this);
        }

        #region Vector to Vector
        public static Vector3D operator +(Vector3D v1)
        {
            return
                (
                    new Vector3D
                    (
                    +v1.X,
                    +v1.Y,
                    +v1.Z
                    )
                    );
        }
        
        public static Vector3D operator -(Vector3D v1)
        {
            return
                (
                    new Vector3D
                    (
                    -v1.X,
                    -v1.Y,
                    -v1.Z
                    )
                    );
        }
        
        
        public static Vector3D operator +(Vector3D v1, Vector3D v2)
        {
            return
                (
                    new Vector3D
                    (
                    v1.X + v2.X,
                    v1.Y + v2.Y,
                    v1.Z + v2.Z
                    )
                    );
        }
        
        public static Vector3D operator -(Vector3D v1, Vector3D v2)
        {
            return
                (
                    new Vector3D
                    (
                    v1.X - v2.X,
                    v1.Y - v2.Y,
                    v1.Z - v2.Z
                    )
                    );
        }
        
        
        public static double operator *(Vector3D v1, Vector3D v2)
        {
            return
                (
                    v1.X * v2.X +
                    v1.Y * v2.Y +
                    v1.Z * v2.Z
                    
                    );
        }
        public static Vector3D operator ^(Vector3D  v1, Vector3D  v2)
        {
            return
                (
                    new Vector3D (v1.Y * v2.Z - v1.Z * v2.Y, v1.Z * v2.X - v1.X * v2.Z, v1.X * v2.Y - v1.Y * v2.X)
                    );
        }
        #endregion Vector to Vector

        public void Normalize()
        {
            float len = Length();
            if (len > 0) {
                X /= len;
                Y /= len;
                Z /= len;
            }
        }

        public override string ToString()
        {
            return string.Format("({0},{1},{2})", X, Y, Z);
        }
    }

    public class Point3D : Vector3D
    {
        public Point3D(): base () {}
        public Point3D(Vector3D v): base (v) {}
        public Point3D(float x, float y, float z): base (x,y,z) {}

        public void Rotate_vector3D()
        {
        }

        public override string ToString()
        {
            return string.Format("<{0},{1},{2}>", X, Y, Z);
        }
    }
    

    //TODO:rorate
   
//            Rotation around any given axis
//            Rotation from normal vector to normal vector
//            Apparently the 5th function is enough, because for example "Rotation around X axis" can be replace by rotation around (1,0,0), and "Rotation around all axes" is merely the product of 3 matrices. BUT, one should use the specific function he or she needs, because it is more efficient.
//                For matrix operations you can use this post.
//                    Code:
//                    public static Matrix4 GetRotationMatrixX(double angle)
//                {
//                    if (angle == 0.0)
//                    {
//                        return Matrix4.I;
//                    }
//                    float sin = (float)Math.Sin(angle);
//                    float cos = (float)Math.Cos(angle);
//                    return new Matrix4(new float[4, 4] {
//                        { 1.0f, 0.0f, 0.0f, 0.0f }, 
//                        { 0.0f, cos, -sin, 0.0f }, 
//                        { 0.0f, sin, cos, 0.0f }, 
//                        { 0.0f, 0.0f, 0.0f, 1.0f } });
//                }
//    
//    public static Matrix4 GetRotationMatrixY(double angle)
//    {
//        if (angle == 0.0)
//        {
//            return Matrix4.I;
//        }
//        float sin = (float)Math.Sin(angle);
//        float cos = (float)Math.Cos(angle);
//        return new Matrix4(new float[4, 4] {
//            { cos, 0.0f, sin, 0.0f }, 
//            { 0.0f, 1.0f, 0.0f, 0.0f }, 
//            { -sin, 0.0f, cos, 0.0f }, 
//            { 0.0f, 0.0f, 0.0f, 1.0f } });
//    }
//    
//    public static Matrix4 GetRotationMatrixZ(double angle)
//    {
//        if (angle == 0.0)
//        {
//            return Matrix4.I;
//        }
//        float sin = (float)Math.Sin(angle);
//        float cos = (float)Math.Cos(angle);
//        return new Matrix4(new float[4, 4] {
//            { cos, -sin, 0.0f, 0.0f }, 
//            { sin, cos, 0.0f, 0.0f }, 
//            { 0.0f, 0.0f, 1.0f, 0.0f }, 
//            { 0.0f, 0.0f, 0.0f, 1.0f } });
//    }
//    
//    public static Matrix4 GetRotationMatrix(double ax, double ay, double az)
//    {
//        Matrix4 my = null;
//        Matrix4 mz = null;
//        Matrix4 result = null;
//        if (ax != 0.0)
//        {
//            result = GetRotationMatrixX(ax);
//        }
//        if (ay != 0.0)
//        {
//            my = GetRotationMatrixY(ay);
//        }
//        if (az != 0.0)
//        {
//            mz = GetRotationMatrixZ(az);
//        }
//        if (my != null)
//        {
//            if (result != null)
//            {
//                result *= my;
//            }
//            else
//            {
//                result = my;
//            }
//        }
//        if (mz != null)
//        {
//            if (result != null)
//            {
//                result *= mz;
//            }
//            else
//            {
//                result = mz;
//            }
//        }
//        if (result != null)
//        {
//            return result;
//        }
//        else
//        {
//            return Matrix4.I;
//        }
//    }
//    
//    public static Matrix4 GetRotationMatrix(Vector3 axis, double angle)
//    {
//        if (angle == 0.0)
//        {
//            return Matrix4.I;
//        }
//        
//        float x = axis.x;
//        float y = axis.y;
//        float z = axis.z;
//        float sin = (float)Math.Sin(angle);
//        float cos = (float)Math.Cos(angle);
//        float xx = x * x;
//        float yy = y * y;
//        float zz = z * z;
//        float xy = x * y;
//        float xz = x * z;
//        float yz = y * z;
//        
//        float[,] matrix = new float[4, 4];
//        
//        matrix[0, 0] = xx + (1 - xx) * cos;
//        matrix[1, 0] = xy * (1 - cos) + z * sin;
//        matrix[2, 0] = xz * (1 - cos) - y * sin;
//        matrix[3, 0] = 0.0f;
//        
//        matrix[0, 1] = xy * (1 - cos) - z * sin;
//        matrix[1, 1] = yy + (1 - yy) * cos;
//        matrix[2, 1] = yz * (1 - cos) + x * sin;
//        matrix[3, 1] = 0.0f;
//        
//        matrix[0, 2] = xz * (1 - cos) + y * sin;
//        matrix[1, 2] = yz * (1 - cos) - x * sin;
//        matrix[2, 2] = zz + (1 - zz) * cos;
//        matrix[3, 2] = 0.0f;
//        
//        matrix[3, 0] = 0.0f;
//        matrix[3, 1] = 0.0f;
//        matrix[3, 2] = 0.0f;
//        matrix[3, 3] = 1.0f;
//        
//        return new Matrix4(matrix);
//    }
//    
//    /// <param name="source">Should be normalized</param>
//    /// <param name="destination">Should be normalized</param>
//    public static Matrix4 GetRotationMatrix(Vector3 source, Vector3 destination)
//    {
//        Vector3 rotaxis = Vector3.CrossProduct(source, destination);
//        if (rotaxis != Vector3.Zero)
//        {
//            rotaxis.Normalize();
//            float cos = source.DotProduct(destination);
//            double angle = Math.Acos(cos);
//            return GetRotationMatrix(rotaxis, angle);
//        }
//        else
//        {
//            return Matrix4.I;
//        }
//    }

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

    public class Matrix3D
    {
        private float[,] values;
        private int rowCount = 3;
        private int columnCount = 3;

        public Matrix3D()
        {
            values = new float[rowCount, columnCount];
        }
        
        public Matrix3D(int rowCount, int columnCount)
        {
            this.rowCount = rowCount;
            this.columnCount = columnCount;
            values = new float[rowCount, columnCount];
        }
        public float this[int row, int column]
        {
            get { return values[row, column]; }
            set { values[row, column] = value; }
        }
        public int RowCount
        {
            get { return rowCount; }
        }

        public int ColumnCount
        {
            get { return columnCount; }
        }
        public Matrix3D Clone()
        {
            Matrix3D resultMatrix = new Matrix3D(rowCount, columnCount);
            for (int i = 0; i < rowCount; i++)
            {
                for (int j = 0; j < columnCount; j++)
                {
                    resultMatrix[i, j] = this[i, j];
                }
            }
            return resultMatrix;
        }
        public Matrix3D Transpose()
        {
            Matrix3D resultMatrix = new Matrix3D(columnCount, rowCount);
            for (int i = 0; i < rowCount; i++)
            {
                for (int j = 0; j < columnCount; j++)
                {
                    resultMatrix[j, i] = this[i, j];
                }
            }
            return resultMatrix;
        }


    }
}

