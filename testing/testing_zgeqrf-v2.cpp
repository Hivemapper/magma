/*
    -- MAGMA (version 1.0) --
       Univ. of Tennessee, Knoxville
       Univ. of California, Berkeley
       Univ. of Colorado, Denver
       November 2010

       @precisions normal z -> s d c

*/

// includes, system
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cublas.h>

#include <quark.h>

// includes, project
#include "magma.h"

#ifndef min
#define min(a,b)  (((a)<(b))?(a):(b))
#endif

#include <pthread.h>

typedef struct {
  int nthreads;
  int nb;
  int np_gpu;
  int m;
  int n;
  int lda;
  cuDoubleComplex *a;
  cuDoubleComplex *t;
  pthread_t *thread;
  volatile cuDoubleComplex **p;
} MAGMA_GLOBALS;



MAGMA_GLOBALS MG;

void *cpu_thread(void *a)
{
  int i;

  long int t = (long int) a;

  int M;
  int N;
  int K;
  cuDoubleComplex *WORK;

//fprintf(stderr,"thread=%d\n",t);

  // traverse panels 
  for (i = 0; i < MG.np_gpu; i++) {
    while (MG.p[i] == NULL) {
      sched_yield();
    }

    M=MG.m-i*MG.nb;
    N=MG.nb;
    K=MG.nb;
	if (MG.m >= MG.n) {
      if (i == (MG.np_gpu - 1)) {
        K = MG.n-MG.nthreads*MG.nb-(MG.np_gpu-1)*MG.nb; 
      }
	}

//fprintf(stderr,"thread=%d panel=%d K=%d\n",t,i,K);

    WORK = (cuDoubleComplex*)malloc(sizeof(cuDoubleComplex)*M*N);

    lapackf77_zlarfb(MagmaLeftStr, MagmaTransStr, MagmaForwardStr, MagmaColumnwiseStr,
                     &M,&N,&K,MG.a+i*MG.nb*MG.lda+i*MG.nb,&MG.lda,MG.t+i*MG.nb*MG.nb,&K,
                     MG.a+MG.m*MG.n-(MG.nthreads-t)*MG.nb*MG.lda+i*MG.nb,&MG.lda,WORK,&N);

    free(WORK);

  }

  return (void*)NULL;
}

void magma_init (int m, int n, cuDoubleComplex *a, int nthreads)
{
  int i;

  MG.nthreads = nthreads;

  if (MG.nb == -1)
    //MG.nb = magma_get_sgeqrf_nb(min(m, n));
    MG.nb = 128;

  if (MG.nb * MG.nthreads >= n){
    fprintf(stderr,"\n\nNumber of threads times block size not less than width of matrix.\n\n");
	exit(1);
  }

  int np = n/MG.nb;
  if (n%MG.nb != 0)
    np++;

  MG.np_gpu = np - MG.nthreads;
  MG.m = m;
  MG.n = n;
  MG.lda = m;
  MG.a = a;
  MG.t = (cuDoubleComplex*)malloc(sizeof(cuDoubleComplex)*MG.n*MG.nb);

  if (MG.n > MG.m) {
    MG.np_gpu = m/MG.nb;
    if (m%MG.nb != 0)
      MG.np_gpu++;
  }

fprintf(stderr,"MG.np_gpu=%d\n",MG.np_gpu);

  MG.p = (volatile cuDoubleComplex **) malloc (MG.np_gpu*sizeof(cuDoubleComplex*));

  for (i = 0; i < MG.np_gpu; i++) {
    MG.p[i] = NULL;
  }

  MG.thread = (pthread_t*)malloc(sizeof(pthread_t)*MG.nthreads);

  for (i = 0; i < MG.nthreads; i++){
    pthread_create(&MG.thread[i], NULL, cpu_thread, (void *)(long int)i);
  }
}

int EN_BEE;

int TRACE;

Quark *quark;

/* ////////////////////////////////////////////////////////////////////////////
   -- Testing zgeqrf
*/
int main( int argc, char** argv) 
{

int nthreads=2;

EN_BEE = 32;

TRACE = 0;

int nquarkthreads=2;


    cuInit( 0 );
    cublasInit( );
    printout_devices( );

    cuDoubleComplex *h_A, *h_R, *h_work, *tau;
    double gpu_perf, cpu_perf;

    TimeStruct start, end;

    /* Matrix size */
    int M=0, N=0, n2;
    int size[10] = {1024,2048,3072,4032,5184,6016,7040,8064,9088,10112};

    cublasStatus status;
    int i, j, info;
    int ione     = 1;
    int ISEED[4] = {0,0,0,1};

    MG.nb=-1;

	int loop = argc;

    int accuracyflag = 1;

    if (argc != 1){
      for(i = 1; i<argc; i++){      
        if (strcmp("-N", argv[i])==0)
          N = atoi(argv[++i]);
        else if (strcmp("-M", argv[i])==0)
          M = atoi(argv[++i]);
        else if (strcmp("-B", argv[i])==0)
          MG.nb = atoi(argv[++i]);
        else if (strcmp("-b", argv[i])==0)
          EN_BEE = atoi(argv[++i]);
        else if (strcmp("-A", argv[i])==0)
          accuracyflag = atoi(argv[++i]);
        else if (strcmp("-P", argv[i])==0)
          nthreads = atoi(argv[++i]);
        else if (strcmp("-Q", argv[i])==0)
          nquarkthreads = atoi(argv[++i]);
      }

      if ((M>0 && N>0) || (M==0 && N==0)) {
        printf("  testing_zgeqrf-v2 -M %d -N %d\n\n", M, N);
        if (M==0 && N==0) {
          M = N = size[9];
          loop = 1;
        }
      } else {
        printf("\nUsage: \n");
        printf("  testing_zgeqrf-v2 -M %d -N %d -B 128 -T 1\n\n", 1024, 1024);
        exit(1);
      }
    } else {
      printf("\nUsage: \n");
      printf("  testing_zgeqrf-v2 -M %d -N %d -B 128 -T 1\n\n", 1024, 1024);
      M = N = size[9];
    }


    /* Initialize CUBLAS */
    status = cublasInit();
    if (status != CUBLAS_STATUS_SUCCESS) {
        fprintf (stderr, "!!!! CUBLAS initialization error\n");
    }

    n2  = M * N;
    int min_mn = min(M, N);

    /* Allocate host memory for the matrix */
    h_A = (cuDoubleComplex*)malloc(n2 * sizeof(h_A[0]));
    if (h_A == 0) {
        fprintf (stderr, "!!!! host memory allocation error (A)\n");
    }

    tau = (cuDoubleComplex*)malloc(min_mn * sizeof(cuDoubleComplex));
    if (tau == 0) {
        fprintf (stderr, "!!!! host memory allocation error (tau)\n");
    }

    cudaMallocHost( (void**)&h_R,  n2*sizeof(cuDoubleComplex) );
    if (h_R == 0) {
        fprintf (stderr, "!!!! host memory allocation error (R)\n");
    }

    int nb = magma_get_zgeqrf_nb(min_mn);
    int lwork = N*nb;

    cudaMallocHost( (void**)&h_work, lwork*sizeof(cuDoubleComplex) );
    //h_work = (cuDoubleComplex*)malloc(lwork * sizeof(cuDoubleComplex));
    if (h_work == 0) {
        fprintf (stderr, "!!!! host memory allocation error (work)\n");
    }

    printf("\n\n");
    printf("  M     N   CPU GFlop/s   GPU GFlop/s    ||R||_F / ||A||_F\n");
    printf("==========================================================\n");
    for(i=0; i<10; i++){
        if (loop==1){
            M = N = min_mn = size[i];
            n2 = M*N;
        }

        /* Initialize the matrix */
        lapackf77_zlarnv( &ione, ISEED, &n2, h_A );
        lapackf77_zlacpy( MagmaUpperLowerStr, &M, &N, h_A, &M, h_R, &M );

        //magma_zgeqrf(M, N, h_R, M, tau, h_work, lwork, &info);

        for(j=0; j<n2; j++)
            h_R[j] = h_A[j];

        /* ====================================================================
           Performs operation using MAGMA
           =================================================================== */

magma_init(M, N, h_R, nthreads);

quark = QUARK_New(nquarkthreads);

        start = get_current_time();
        magma_zgeqrf3(M, N, h_R, M, tau, h_work, lwork, &info);
        end = get_current_time();

QUARK_Delete(quark);

        gpu_perf = 4.*M*N*min_mn/(3.*1000000*GetTimerValue(start,end));
        // printf("GPU Processing time: %f (ms) \n", GetTimerValue(start,end));
        
        /* =====================================================================
           Check the factorization
           =================================================================== */
        /*
          cuDoubleComplex result[2];

          cuDoubleComplex *hwork_Q = (cuDoubleComplex*)malloc( M * N * sizeof(cuDoubleComplex));
          cuDoubleComplex *hwork_R = (cuDoubleComplex*)malloc( M * N * sizeof(cuDoubleComplex));
          cuDoubleComplex *rwork   = (cuDoubleComplex*)malloc( N * sizeof(cuDoubleComplex));

          lapackf77_zqrt02(&M, &min_mn, &min_mn, h_A, h_R, hwork_Q, hwork_R, &M, tau,
          h_work, &lwork, rwork, result);

          printf("norm( R - Q'*A ) / ( M * norm(A) * EPS ) = %f\n", result[0]);
          printf("norm( I - Q'*Q ) / ( M * EPS )           = %f\n", result[1]);
          free(hwork_Q);
          free(hwork_R);
          free(rwork);
        */
        /* =====================================================================
           Performs operation using LAPACK
           =================================================================== */
        start = get_current_time();
        if (accuracyflag == 1)
          lapackf77_zgeqrf(&M, &N, h_A, &M, tau, h_work, &lwork, &info);
        end = get_current_time();
        if (info < 0)
            printf("Argument %d of zgeqrf had an illegal value.\n", -info);

        cpu_perf = 4.*M*N*min_mn/(3.*1000000*GetTimerValue(start,end));
        // printf("CPU Processing time: %f (ms) \n", GetTimerValue(start,end));

        /* =====================================================================
           Check the result compared to LAPACK
           =================================================================== */
        double work[1], matnorm = 1.;
        cuDoubleComplex mone = MAGMA_Z_NEG_ONE;
        int one = 1;

        if (accuracyflag == 1){
          matnorm = lapackf77_zlange("f", &M, &N, h_A, &M, work);
          blasf77_zaxpy(&n2, &mone, h_A, &one, h_R, &one);
        }

        if (accuracyflag == 1){
          printf("%5d %5d  %6.2f         %6.2f        %e\n",
                 M, N, cpu_perf, gpu_perf,
                 lapackf77_zlange("f", &M, &N, h_R, &M, work) / matnorm);
        } else {
          printf("%5d %5d                %6.2f          \n",
                 M, N, gpu_perf);
        }

        /* =====================================================================
           Print performance and error.
           =================================================================== */
        /*
          printf("%5d    %6.2f         %6.2f        %e\n",
          N, cpu_perf, gpu_perf,
          N*result[0]*5.96e-08);
        */

        if (loop != 1)
            break;
    }

    /* Memory clean up */
    free(h_A);
    free(tau);
    cublasFree(h_work);
    cublasFree(h_R);

    /* Shutdown */
    status = cublasShutdown();
    if (status != CUBLAS_STATUS_SUCCESS) {
        fprintf (stderr, "!!!! shutdown error (A)\n");
    }
}
