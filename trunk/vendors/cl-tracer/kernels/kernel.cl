//#pragma OPENCL EXTENSION cl_amd_printf : enable


bool isInside(float4 ray)
{
    float x = ray.x + 0.0;
    float y = ray.y + 0.0;
    float z = ray.z + 40.0;
    float r = 16;

    return  ( r * r - x*x - y*y - z*z) > 0.0;
}

float4 mul(__global float4* matrix, float4 vector)
{
        float4 result;
        result.x = matrix[0].x * vector.x + 
                   matrix[0].y * vector.y +
                   matrix[0].z * vector.z;
                   
        result.y = matrix[1].x * vector.x + 
                   matrix[1].y * vector.y +
                   matrix[1].z * vector.z;
                   
        result.z = matrix[2].x * vector.x + 
                   matrix[2].y * vector.y +
                   matrix[2].z * vector.z;
        result.w = 1;
        return result;

}

/*
void print_float4(float4 f)
{
        //printf("%f, %f, %f\n", f.x, f.y, f.z);
}

void print_mat4(__global float4* f)
{
        printf("%f, %f, %f\n, %f, %f, %f\n, %f, %f, %f\n\n", 
        f[0].x, f[0].y, f[0].z, 
        f[1].x, f[1].y, f[1].z,
        f[2].x, f[2].y, f[2].z);
}
*/
__kernel void trace(__global float4* rays, __global uchar4* output, __global float4* camera)
{
       float3 light = (float3)(60, 0, 0);
       float3 normal; 
       bool hasHit = false;

       int i = 0;
       int x = get_global_id(0);
       int y = get_global_id(1);

       float4 ray = mul(camera, rays[x + get_global_size(0)*y]);
       float4 start = (float4) (camera[3]);

       while(length(start) < 100.0)
       {
            start += ray*0.1;
            if(traceAll(start, &normal)) 
            {
                hasHit = true;
                break;
            }
       }

       if(hasHit)
       {
           float3 lightDir = normalize(light - start.xyz).xyz;
           float k = max(0.0f, (dot(normalize(normal),lightDir) ));            
           uchar4 color = (uchar4)(0, 255*k, 0, 255*k);

           output[x + get_global_size(0)*y] = color;
           return ;
       }

       output[x + get_global_size(0)*y] = (uchar4)(127, 127, 127, 1.0);
}

