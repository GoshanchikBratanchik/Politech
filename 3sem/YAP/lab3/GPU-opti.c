#ifdef _WIN32
#  define _CRT_SECURE_NO_WARNINGS
#  include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef __APPLE__
#  include <OpenCL/cl.h>
#else
#  include <CL/cl.h>
#endif

#define CHECK(status, msg)                                         \
    if ((status) != CL_SUCCESS) {                                  \
        fprintf(stderr, "%s failed (err=%d)\n", (msg), (status)); \
        exit(-1);                                                  \
    }

static double now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
#endif
}

static int load_bmp_color(const char* path, int* w, int* h,
    unsigned char** R,
    unsigned char** G,
    unsigned char** B)
{
    FILE* f = fopen(path, "rb");
    unsigned char hdr[54];
    int bpp, pixel_offset, row_padded, x, y;
    size_t npix;
    unsigned char* row;

    if (!f) {
        fprintf(stderr, "Cannot open '%s'\n", path);
        fprintf(stderr, "Put the .bmp file in the same folder as the .exe\n");
        return -1;
    }
    if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        fprintf(stderr, "'%s' is not a valid BMP file\n", path);
        fclose(f); return -1;
    }

    pixel_offset = hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | (hdr[13] << 24);
    *w = hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24);
    *h = hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24);
    bpp = hdr[28] | (hdr[29] << 8);

    if (bpp != 24) {
        fprintf(stderr, "Unsupported BMP format: %d bpp\n", bpp);
        fprintf(stderr, "Save image as 24-bit BMP in Paint or GIMP\n");
        fclose(f); return -1;
    }

    npix = (size_t)(*w) * (*h);
    *R = (unsigned char*)malloc(npix);
    *G = (unsigned char*)malloc(npix);
    *B = (unsigned char*)malloc(npix);
    if (!*R || !*G || !*B) { perror("malloc"); fclose(f); return -1; }

    row_padded = ((*w) * 3 + 3) & ~3;
    row = (unsigned char*)malloc(row_padded);

    fseek(f, pixel_offset, SEEK_SET);

    for (y = (*h) - 1; y >= 0; y--) {
        fread(row, 1, row_padded, f);
        for (x = 0; x < *w; x++) {

            (*B)[y * (*w) + x] = row[x * 3 + 0];
            (*G)[y * (*w) + x] = row[x * 3 + 1];
            (*R)[y * (*w) + x] = row[x * 3 + 2];
        }
    }
    free(row);
    fclose(f);
    return 0;
}

static void save_bmp_color(const char* path, int w, int h,
    const unsigned char* R,
    const unsigned char* G,
    const unsigned char* B)
{
    FILE* f = fopen(path, "wb");
    unsigned char hdr[54];
    int row_padded, file_size, x, y;
    unsigned char* row;

    if (!f) { perror(path); return; }

    row_padded = (w * 3 + 3) & ~3;
    file_size = 54 + row_padded * h;

    memset(hdr, 0, 54);
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (file_size) & 0xFF; hdr[3] = (file_size >> 8) & 0xFF;
    hdr[4] = (file_size >> 16) & 0xFF; hdr[5] = (file_size >> 24) & 0xFF;
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = w & 0xFF; hdr[19] = (w >> 8) & 0xFF; hdr[20] = (w >> 16) & 0xFF; hdr[21] = (w >> 24) & 0xFF;
    hdr[22] = h & 0xFF; hdr[23] = (h >> 8) & 0xFF; hdr[24] = (h >> 16) & 0xFF; hdr[25] = (h >> 24) & 0xFF;
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, f);

    row = (unsigned char*)malloc(row_padded);
    memset(row, 0, row_padded);

    for (y = h - 1; y >= 0; y--) {
        for (x = 0; x < w; x++) {
            row[x * 3 + 0] = B[y * w + x];
            row[x * 3 + 1] = G[y * w + x];
            row[x * 3 + 2] = R[y * w + x];
        }
        fwrite(row, 1, row_padded, f);
    }
    free(row);
    fclose(f);
}

static void make_gaussian(int sz, float* out) {
    int R = sz / 2, ky, kx, i;
    float sigma = sz / 6.0f, s2 = 2.0f * sigma * sigma, total = 0.0f;
    for (ky = -R; ky <= R; ky++)
        for (kx = -R; kx <= R; kx++) {
            float w = expf(-(ky * ky + kx * kx) / s2);
            out[(ky + R) * sz + (kx + R)] = w;
            total += w;
        }
    for (i = 0; i < sz * sz; i++) out[i] /= total;
}

static unsigned char* convolve_cpu_channel(
    const unsigned char* img, int w, int h,
    const float* kdata, int ksz)
{
    int x, y, kx, ky, R = ksz / 2;
    unsigned char* out = (unsigned char*)malloc((size_t)w * h);
    if (!out) { perror("malloc"); exit(-1); }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            float sum = 0.0f;
            for (ky = -R; ky <= R; ky++)
                for (kx = -R; kx <= R; kx++) {
                    int ix = x + kx < 0 ? 0 : x + kx >= w ? w - 1 : x + kx;
                    int iy = y + ky < 0 ? 0 : y + ky >= h ? h - 1 : y + ky;
                    sum += img[iy * w + ix] * kdata[(ky + R) * ksz + (kx + R)];
                }
            out[y * w + x] = (unsigned char)(sum < 0.0f ? 0 : sum > 255.0f ? 255 : sum);
        }
    return out;
}

static double convolve_cpu_color(
    const unsigned char* iR, const unsigned char* iG, const unsigned char* iB,
    int w, int h, const float* kdata, int ksz,
    unsigned char** oR, unsigned char** oG, unsigned char** oB)
{
    double t0 = now_ms();
    *oR = convolve_cpu_channel(iR, w, h, kdata, ksz);
    *oG = convolve_cpu_channel(iG, w, h, kdata, ksz);
    *oB = convolve_cpu_channel(iB, w, h, kdata, ksz);
    return now_ms() - t0;
}

typedef struct {
    cl_context       ctx;
    cl_command_queue queue;
    cl_program       program;
    cl_kernel        klocal;
    cl_device_id     device;
    size_t           maxWGSize;
    cl_ulong         localMemBytes;
    char             platName[128];
} OCL;

static const char* KERNEL_SOURCE =
"__kernel void convolve_local(\n"
"    __global  const uchar* input,\n"
"    __global        uchar* output,\n"
"    __constant      float* kernel_data,\n"
"    const int img_w,\n"
"    const int img_h,\n"
"    const int ksize,\n"
"    __local         float* tile\n"
")\n"
"{\n"
"    int gx = get_global_id(0);\n"
"    int gy = get_global_id(1);\n"
"    int lx = get_local_id(0);\n"
"    int ly = get_local_id(1);\n"
"    int lw = get_local_size(0);\n"
"    int lh = get_local_size(1);\n"
"\n"
"    int rad    = ksize / 2;\n"
"    int tile_w = lw + ksize - 1;\n"
"    int tile_h = lh + ksize - 1;\n"
"    int orig_x = get_group_id(0) * lw - rad;\n"
"    int orig_y = get_group_id(1) * lh - rad;\n"
"\n"
"    for (int ty = ly; ty < tile_h; ty += lh) {\n"
"        for (int tx = lx; tx < tile_w; tx += lw) {\n"
"            int sx = clamp(orig_x + tx, 0, img_w - 1);\n"
"            int sy = clamp(orig_y + ty, 0, img_h - 1);\n"
"            tile[ty * tile_w + tx] = (float)input[sy * img_w + sx];\n"
"        }\n"
"    }\n"
"\n"
"    barrier(CLK_LOCAL_MEM_FENCE);\n"
"\n"
"    if (gx < img_w && gy < img_h) {\n"
"        float sum = 0.0f;\n"
"        for (int ky = 0; ky < ksize; ky++)\n"
"            for (int kx = 0; kx < ksize; kx++)\n"
"                sum += tile[(ly + ky) * tile_w + (lx + kx)]\n"
"                     * kernel_data[ky * ksize + kx];\n"
"        output[gy * img_w + gx] = (uchar)clamp(sum, 0.0f, 255.0f);\n"
"    }\n"
"}\n";

static OCL ocl_init(void) {
    OCL o;
    cl_int status;
    cl_uint numPlatforms = 0;
    cl_platform_id* platforms;
    cl_uint numDevices = 0;
    cl_device_id* devices;
    int selected = 0, i;
    size_t srcLen = 0;

    status = clGetPlatformIDs(0, NULL, &numPlatforms);
    if (status != CL_SUCCESS) {
        printf("clGetPlatformIDs failed\n");
        exit(-1);
    }
    if (numPlatforms == 0) {
        printf("No OpenCL platforms. Install GPU drivers.\n");
        exit(-1);
    }

    platforms = (cl_platform_id*)malloc(numPlatforms * sizeof(cl_platform_id));
    if (platforms == NULL) {
        perror("malloc");
        exit(-1);
    }

    status = clGetPlatformIDs(numPlatforms, platforms, NULL);
    if (status != CL_SUCCESS) {
        printf("clGetPlatformIDs failed\n");
        exit(-1);
    }

    for (i = 0; i < (int)numPlatforms; i++) {
        char buf[100];
        status = clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, sizeof(buf), buf, NULL);
        status = clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(buf), buf, NULL);
        if (strstr(buf, "NVIDIA") != NULL || strstr(buf, "AMD") != NULL)
            selected = i;
    }

    clGetPlatformInfo(platforms[selected], CL_PLATFORM_NAME,
        sizeof(o.platName), o.platName, NULL);

    status = clGetDeviceIDs(platforms[selected], CL_DEVICE_TYPE_GPU,
        0, NULL, &numDevices);
    if (status != CL_SUCCESS) {
        printf("clGetDeviceIDs failed\n");
        exit(-1);
    }

    if (numDevices == 0) {
        printf("No GPU found, falling back to CPU\n");
        status = clGetDeviceIDs(platforms[selected], CL_DEVICE_TYPE_CPU,
            0, NULL, &numDevices);
        if (status != CL_SUCCESS) {
            printf("clGetDeviceIDs failed\n");
            exit(-1);
        }
    }

    devices = (cl_device_id*)malloc(numDevices * sizeof(cl_device_id));
    if (devices == NULL) {
        perror("malloc");
        exit(-1);
    }

    status = clGetDeviceIDs(platforms[selected], CL_DEVICE_TYPE_GPU,
        numDevices, devices, NULL);
    if (status != CL_SUCCESS)
        status = clGetDeviceIDs(platforms[selected], CL_DEVICE_TYPE_CPU,
            numDevices, devices, NULL);
    if (status != CL_SUCCESS) {
        printf("clGetDeviceIDs failed\n");
        exit(-1);
    }

    o.device = devices[0];
    free(platforms);
    free(devices);
    clGetDeviceInfo(o.device, CL_DEVICE_MAX_WORK_GROUP_SIZE,
        sizeof(size_t), &o.maxWGSize, NULL);
    clGetDeviceInfo(o.device, CL_DEVICE_LOCAL_MEM_SIZE,
        sizeof(cl_ulong), &o.localMemBytes, NULL);

    o.ctx = clCreateContext(NULL, 1, &o.device, NULL, NULL, &status);
    CHECK(status, "clCreateContext");
    {
        cl_queue_properties qp[] = {
            CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0
        };
        o.queue = clCreateCommandQueueWithProperties(o.ctx, o.device, qp, &status);
        CHECK(status, "clCreateCommandQueueWithProperties");
    }
    srcLen = strlen(KERNEL_SOURCE);
    o.program = clCreateProgramWithSource(o.ctx, 1,
        &KERNEL_SOURCE, &srcLen, &status);
    CHECK(status, "clCreateProgramWithSource");
    {
        cl_int err = clBuildProgram(o.program, 1, &o.device,
            "-cl-fast-relaxed-math", NULL, NULL);
        if (err != CL_SUCCESS) {
            size_t logSz = 0; char* log;
            clGetProgramBuildInfo(o.program, o.device,
                CL_PROGRAM_BUILD_LOG, 0, NULL, &logSz);
            log = (char*)malloc(logSz + 1);
            clGetProgramBuildInfo(o.program, o.device,
                CL_PROGRAM_BUILD_LOG, logSz, log, NULL);
            fprintf(stderr, "Build error:\n%s\n", log);
            free(log); exit(-1);
        }
    }
    o.klocal = clCreateKernel(o.program, "convolve_local", &status);
    CHECK(status, "clCreateKernel(local)");
    return o;
}

static void ocl_free(OCL* o) {
    clReleaseKernel(o->klocal);
    clReleaseProgram(o->program);
    clReleaseCommandQueue(o->queue);
    clReleaseContext(o->ctx);
}

static double gpu_blur_channel(
    OCL* o,
    const unsigned char* input, int w, int h,
    const float* kdata, int ksz,
    unsigned char* output)
{
    cl_int  status;
    size_t  img_bytes = (size_t)w * h;
    size_t  kern_bytes = (size_t)ksz * ksz * sizeof(float);
    size_t  lx = (o->maxWGSize >= 256) ? 16 : 8, ly = lx;
    size_t  gx = ((w + lx - 1) / lx) * lx, gy = ((h + ly - 1) / ly) * ly;
    size_t  global[2] = { gx, gy }, local_sz[2] = { lx, ly };
    size_t  localmem = (lx + ksz - 1) * (ly + ksz - 1) * sizeof(float);
    cl_mem  buf_in, buf_out, buf_kern;
    const int ITERS = 5;
    double  total_ms = 0.0;
    int     i;
    cl_event ev;

    buf_in = clCreateBuffer(o->ctx,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        img_bytes, (void*)input, &status);
    CHECK(status, "clCreateBuffer(in)");
    buf_out = clCreateBuffer(o->ctx, CL_MEM_WRITE_ONLY,
        img_bytes, NULL, &status);
    CHECK(status, "clCreateBuffer(out)");
    buf_kern = clCreateBuffer(o->ctx,
        CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        kern_bytes, (void*)kdata, &status);
    CHECK(status, "clCreateBuffer(kern)");

    clSetKernelArg(o->klocal, 0, sizeof(cl_mem), &buf_in);
    clSetKernelArg(o->klocal, 1, sizeof(cl_mem), &buf_out);
    clSetKernelArg(o->klocal, 2, sizeof(cl_mem), &buf_kern);
    clSetKernelArg(o->klocal, 3, sizeof(int), &w);
    clSetKernelArg(o->klocal, 4, sizeof(int), &h);
    clSetKernelArg(o->klocal, 5, sizeof(int), &ksz);
    clSetKernelArg(o->klocal, 6, localmem, NULL);


    CHECK(clEnqueueNDRangeKernel(o->queue, o->klocal, 2, NULL,
        global, local_sz, 0, NULL, &ev), "warmup");
    clWaitForEvents(1, &ev); clReleaseEvent(ev);

    for (i = 0; i < ITERS; i++) {
        cl_ulong ts, te;
        CHECK(clEnqueueNDRangeKernel(o->queue, o->klocal, 2, NULL,
            global, local_sz, 0, NULL, &ev), "run");
        clWaitForEvents(1, &ev);
        clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_START, sizeof(ts), &ts, NULL);
        clGetEventProfilingInfo(ev, CL_PROFILING_COMMAND_END, sizeof(te), &te, NULL);
        total_ms += (double)(te - ts) / 1e6;
        clReleaseEvent(ev);
    }

    CHECK(clEnqueueReadBuffer(o->queue, buf_out, CL_TRUE, 0,
        img_bytes, output, 0, NULL, NULL), "readBuffer");

    clReleaseMemObject(buf_in);
    clReleaseMemObject(buf_out);
    clReleaseMemObject(buf_kern);
    return total_ms / ITERS;
}

static double gpu_blur_color(
    OCL* o,
    const unsigned char* iR, const unsigned char* iG, const unsigned char* iB,
    int w, int h, const float* kdata, int ksz,
    unsigned char* oR, unsigned char* oG, unsigned char* oB)
{
    double t = 0.0;
    t += gpu_blur_channel(o, iR, w, h, kdata, ksz, oR);
    t += gpu_blur_channel(o, iG, w, h, kdata, ksz, oG);
    t += gpu_blur_channel(o, iB, w, h, kdata, ksz, oB);
    return t;
}

int main(void) {
    const char* bmp_path = "test_input.bmp";
    int img_w = 0, img_h = 0;
    unsigned char* iR, * iG, * iB;
    unsigned char* oR, * oG, * oB;
    OCL ocl;
    float  kdata[51 * 51];
    static const int kernel_sizes[] = { 7, 15, 21, 35, 51 };
    const int num_kernels = 5;
    double cpu_times[5];
    double gpu_times[5];
    char   devName[256];
    char   label[8];
    char   path[64];
    size_t npix;
    size_t name_len;
    int    k;


    name_len = strlen(bmp_path);
    if (name_len < 4 ||
        bmp_path[name_len - 4] != '.' ||
        (bmp_path[name_len - 3] != 'b' && bmp_path[name_len - 3] != 'B') ||
        (bmp_path[name_len - 2] != 'm' && bmp_path[name_len - 2] != 'M') ||
        (bmp_path[name_len - 1] != 'p' && bmp_path[name_len - 1] != 'P'))
    {
        fprintf(stderr, "Error: INPUT_FILE must end with .bmp\n");
        fprintf(stderr, "Current value: \"%s\"\n", bmp_path);
        fprintf(stderr, "Change the #define INPUT_FILE line at the top of main()\n");
        return 1;
    }

    if (load_bmp_color(bmp_path, &img_w, &img_h, &iR, &iG, &iB) != 0)
        return 1;

    npix = (size_t)img_w * img_h;



    ocl = ocl_init();
    clGetDeviceInfo(ocl.device, CL_DEVICE_NAME, sizeof(devName), devName, NULL);


    oR = (unsigned char*)malloc(npix);
    oG = (unsigned char*)malloc(npix);
    oB = (unsigned char*)malloc(npix);


    for (k = 0; k < num_kernels; k++) {
        int ksz = kernel_sizes[k];
        unsigned char* cR, * cG, * cB;

        make_gaussian(ksz, kdata);

        cpu_times[k] = convolve_cpu_color(iR, iG, iB, img_w, img_h,
            kdata, ksz, &cR, &cG, &cB);
        sprintf(path, "out_cpu_%dx%d.bmp", ksz, ksz);
        save_bmp_color(path, img_w, img_h, cR, cG, cB);
        free(cR); free(cG); free(cB);

        gpu_times[k] = gpu_blur_color(&ocl, iR, iG, iB, img_w, img_h,
            kdata, ksz, oR, oG, oB);
        sprintf(path, "out_gpu_%dx%d.bmp", ksz, ksz);
        save_bmp_color(path, img_w, img_h, oR, oG, oB);
    }

    printf("Image    : %s  (%d x %d)\n", bmp_path, img_w, img_h);
    printf("Platform : %s\n", ocl.platName);
    printf("Device   : %s\n", devName);
    printf("\n");
    printf("+--------+-----------+-----------+----------+\n");
    printf("| Kernel |  CPU, ms  |  GPU, ms  |  CPU/GPU |\n");
    printf("+--------+-----------+-----------+----------+\n");
    for (k = 0; k < num_kernels; k++) {
        sprintf(label, "%dx%d", kernel_sizes[k], kernel_sizes[k]);
        printf("| %-6s | %9.2f | %9.2f | %7.1f   |\n",
            label, cpu_times[k], gpu_times[k],
            cpu_times[k] / gpu_times[k]);
    }
    printf("+--------+-----------+-----------+----------+\n");
    printf("\n");
    printf("Saved: out_cpu_NxN.bmp  out_gpu_NxN.bmp\n");
    printf("       for N = %d, %d, %d, %d, %d\n", kernel_sizes[0], kernel_sizes[1], kernel_sizes[2], kernel_sizes[3], kernel_sizes[4]);

    free(iR); free(iG); free(iB);
    free(oR); free(oG); free(oB);
    ocl_free(&ocl);
    return 0;
}