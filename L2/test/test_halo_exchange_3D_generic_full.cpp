
/*
Copyright (c) 2012, MAURO BIANCO, UGO VARETTO, SWISS NATIONAL SUPERCOMPUTING CENTRE (CSCS)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Swiss National Supercomputing Centre (CSCS) nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL MAURO BIANCO, UGO VARETTO, OR 
SWISS NATIONAL SUPERCOMPUTING CENTRE (CSCS), BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include <mpi.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <halo_exchange.h>
#include <string>
#include <stdlib.h>
#include <utils/layout_map.h>
#include <utils/boollist.h>
#include <sys/time.h>

 //#define USE_DOUBLE true
#define USE_DOUBLE false
#include "triplet.h"

int pid;
int nprocs;
MPI_Comm CartComm;
int dims[3] = {0,0,0};
int coords[3]={0,0,0};

struct timeval start_tv;
struct timeval stop1_tv;
struct timeval stop2_tv;
struct timeval stop3_tv;
double lapse_time1;
double lapse_time2;
double lapse_time3;
double lapse_time4;

#define B_ADD 1
#define C_ADD 2

//#define VECTOR_INTERFACE
#ifdef VECTOR_INTERFACE
typedef int T1;
typedef int T2;
typedef int T3;
#else
typedef int T1;
typedef double T2;
typedef long long int T3;
#endif

typedef GCL::gcl_cpu arch_type;

template <typename T, typename lmap>
struct array {
  T *ptr;
  int n,m,l;
  int size;

  array(T* _p, int _n, int _m, int _l)
    : ptr(_p)
    , n(lmap::template find<2>(_n,_m,_l))
    , m(lmap::template find<1>(_n,_m,_l))
    , l(lmap::template find<0>(_n,_m,_l))  
    , size(_n*_m*_l)
  {}

  T &operator()(int i, int j, int k) {
    assert(lmap::template find<2>(i,j,k)>=0 && lmap::template find<2>(i,j,k)<n);
    assert(lmap::template find<1>(i,j,k)>=0 && lmap::template find<1>(i,j,k)<m);
    assert(lmap::template find<0>(i,j,k)>=0 && lmap::template find<0>(i,j,k)<l);
    assert(l*m*lmap::template find<2>(i,j,k)+l*lmap::template find<1>(i,j,k)+lmap::template find<0>(i,j,k) < size);
    assert(l*m*lmap::template find<2>(i,j,k)+l*lmap::template find<1>(i,j,k)+lmap::template find<0>(i,j,k) >= 0);
    return ptr[l*m*lmap::template find<2>(i,j,k)+
               l*lmap::template find<1>(i,j,k)+
               lmap::template find<0>(i,j,k)];
  }

  T const &operator()(int i, int j, int k) const {
    assert(lmap::template find<2>(i,j,k)>=0 && lmap::template find<2>(i,j,k)<n);
    assert(lmap::template find<1>(i,j,k)>=0 && lmap::template find<1>(i,j,k)<m);
    assert(lmap::template find<0>(i,j,k)>=0 && lmap::template find<0>(i,j,k)<l);
    assert(l*m*lmap::template find<2>(i,j,k)+l*lmap::template find<1>(i,j,k)+lmap::template find<0>(i,j,k) < size);
    assert(l*m*lmap::template find<2>(i,j,k)+l*lmap::template find<1>(i,j,k)+lmap::template find<0>(i,j,k) >= 0);
    return ptr[l*m*lmap::template find<2>(i,j,k)+
               l*lmap::template find<1>(i,j,k)+
               lmap::template find<0>(i,j,k)];
  }

  operator void*() const {return reinterpret_cast<void*>(ptr);}
  operator T*() const {return ptr;}
};

/** \file Example of use of halo_exchange pattern for regular
    grids. The comments in the code aim at highlight the process of
    instantiating and running a halo exchange pattern.
*/

inline int modulus(int __i, int __j) {
  return (((((__i%__j)<0)?(__j+__i%__j):(__i%__j))));
}

/* Just and utility to print values
 */
template <typename array_t>
void printbuff(std::ostream &file, array_t const & a, int d1, int d2, int d3) {
  if (d1<=7 && d2<=7 && d3<=7) {
    file << "------------\n";
    for (int kk=0; kk<d3; ++kk) {
      file << "|";
      for (int jj=0; jj<d2; ++jj) {
        for (int ii=0; ii<d1; ++ii) {
          file << a(ii,jj,kk);
        }
        file << "|\n";
      }
      file << "\n\n";
    }
    file << "------------\n\n";
  }
}


template <typename ST, int I1, int I2, int I3, bool per0, bool per1, bool per2>
void run(ST & file, int DIM1, int DIM2, int DIM3, 
         int H1m1, int H1p1, int H2m1, int H2p1, int H3m1, int H3p1, 
         int H1m2, int H1p2, int H2m2, int H2p2, int H3m2, int H3p2, 
         int H1m3, int H1p3, int H2m3, int H2p3, int H3m3, int H3p3, 
         triple_t<USE_DOUBLE, T1> *_a, triple_t<USE_DOUBLE, T2> *_b, triple_t<USE_DOUBLE, T3> *_c) {

  typedef GCL::layout_map<I1,I2,I3> layoutmap;
  
  array<triple_t<USE_DOUBLE, T1>, layoutmap > a(_a, (DIM1+H1m1+H1p1),(DIM2+H2m1+H2p1),(DIM3+H3m1+H3p1));
  array<triple_t<USE_DOUBLE, T2>, layoutmap > b(_b, (DIM1+H1m2+H1p2),(DIM2+H2m2+H2p2),(DIM3+H3m2+H3p2));
  array<triple_t<USE_DOUBLE, T3>, layoutmap > c(_c, (DIM1+H1m3+H1p3),(DIM2+H2m3+H2p3),(DIM3+H3m3+H3p3));

  /* The pattern type is defined with the layouts, data types and
     number of dimensions.

     The logical assumption done in the program is that 'i' is the
     first dimension (rows), 'j' is the second, and 'k' is the
     third. The first layout states that 'i' is the second dimension
     in order of strides, while 'j' is the first and 'k' is the third
     (just by looking at the initialization loops this shoule be
     clear).

     The second layout states that the first dimension in data ('i')
     identify also the first dimension in the communicator. Logically,
     moving on 'i' dimension from processot (p,q,r) will lead you
     logically to processor (p+1,q,r). The other dimensions goes as
     the others.
   */
  typedef GCL::halo_exchange_generic<GCL::layout_map<0,1,2>, 3, arch_type, GCL::version_manual > pattern_type;


  /* The pattern is now instantiated with the periodicities and the
     communicator. The periodicity of the communicator is
     irrelevant. Setting it to be periodic is the best choice, then
     GCL can deal with any periodicity easily.
  */
  pattern_type he(typename pattern_type::grid_type::period_type(per0, per1, per2), CartComm);


  GCL::array<GCL::halo_descriptor,3> halo_dsc1;
  halo_dsc1[0] = GCL::halo_descriptor(H1m1, H1p1, H1m1, DIM1+H1m1-1, DIM1+H1m1+H1p1);
  halo_dsc1[1] = GCL::halo_descriptor(H2m1, H2p1, H2m1, DIM2+H2m1-1, DIM2+H2m1+H2p1);
  halo_dsc1[2] = GCL::halo_descriptor(H3m1, H3p1, H3m1, DIM3+H3m1-1, DIM3+H3m1+H3p1);

  GCL::array<GCL::halo_descriptor,3> halo_dsc2;
  halo_dsc2[0] = GCL::halo_descriptor(H1m2, H1p2, H1m2, DIM1+H1m2-1, DIM1+H1m2+H1p2);
  halo_dsc2[1] = GCL::halo_descriptor(H2m2, H2p2, H2m2, DIM2+H2m2-1, DIM2+H2m2+H2p2);
  halo_dsc2[2] = GCL::halo_descriptor(H3m2, H3p2, H3m2, DIM3+H3m2-1, DIM3+H3m2+H3p2);

  GCL::array<GCL::halo_descriptor,3> halo_dsc3;
  halo_dsc3[0] = GCL::halo_descriptor(H1m3, H1p3, H1m3, DIM1+H1m3-1, DIM1+H1m3+H1p3);
  halo_dsc3[1] = GCL::halo_descriptor(H2m3, H2p3, H2m3, DIM2+H2m3-1, DIM2+H2m3+H2p3);
  halo_dsc3[2] = GCL::halo_descriptor(H3m3, H3p3, H3m3, DIM3+H3m3-1, DIM3+H3m3+H3p3);

  GCL::field_on_the_fly<triple_t<USE_DOUBLE, T1>::data_type, layoutmap, pattern_type::traits> field1(reinterpret_cast<triple_t<USE_DOUBLE, T1>::data_type*>(a.ptr), halo_dsc1);
  GCL::field_on_the_fly<triple_t<USE_DOUBLE, T2>::data_type, layoutmap, pattern_type::traits> field2(reinterpret_cast<triple_t<USE_DOUBLE, T2>::data_type*>(b.ptr), halo_dsc2);
  GCL::field_on_the_fly<triple_t<USE_DOUBLE, T3>::data_type, layoutmap, pattern_type::traits> field3(reinterpret_cast<triple_t<USE_DOUBLE, T3>::data_type*>(c.ptr), halo_dsc3);

  /* Pattern is set up. This must be done only once per pattern. The
     parameter must me greater or equal to the largest number of
     arrays updated in a single step.
  */
  //he.setup(100, halo_dsc, sizeof(double));

  GCL::array<GCL::halo_descriptor,3> h_example;
#define MAX3(a,b,c) std::max(a, std::max(b,c))
  h_example[0] = GCL::halo_descriptor(MAX3(H1m1, H1m2, H1m3), 
                                      MAX3(H1p1, H1p2, H1p3), 
                                      MAX3(H1m1, H1m2, H1m3), 
                                      DIM1+MAX3(H1m1, H1m2, H1m3)-1, 
                                      DIM1+MAX3(H1m1, H1m2, H1m3)+MAX3(H1p1, H1p3, H1p3));
  h_example[1] = GCL::halo_descriptor(MAX3(H2m1, H2m2, H2m3), 
                                      MAX3(H2p1, H2p2, H2p3), 
                                      MAX3(H2m1, H2m2, H2m3), 
                                      DIM2+MAX3(H2m1, H2m2, H2m3)-1, 
                                      DIM2+MAX3(H2m1, H2m2, H2m3)+MAX3(H2p1, H2p3, H2p3));
  h_example[2] = GCL::halo_descriptor(MAX3(H3m1, H3m2, H3m3), 
                                      MAX3(H3p1, H3p2, H3p3), 
                                      MAX3(H3m1, H3m2, H3m3), 
                                      DIM3+MAX3(H3m1, H3m2, H3m3)-1, 
                                      DIM3+MAX3(H3m1, H3m2, H3m3)+MAX3(H3p1, H3p3, H3p3));
#undef MAX3
  he.setup(3, GCL::field_on_the_fly<int,layoutmap, pattern_type::traits>(NULL,h_example), // BEWARE!!!! 
           std::max(sizeof(triple_t<USE_DOUBLE, T1>::data_type), 
                    std::max(sizeof(triple_t<USE_DOUBLE, T2>::data_type), 
                             sizeof(triple_t<USE_DOUBLE, T3>::data_type)
                             ) // Estimates the size
                    )
           );

  file << "Proc: (" << coords[0] << ", " << coords[1] << ", " << coords[2] << ")\n";

  /* Just an initialization */
  for (int ii=0; ii<DIM1+H1m1+H1p1; ++ii)
    for (int jj=0; jj<DIM2+H2m1+H2p1; ++jj) {
      for (int kk=0; kk<DIM3+H3m1+H3p1; ++kk) {
        a(ii,jj,kk) = triple_t<USE_DOUBLE, T1>();
      }
    }

  for (int ii=0; ii<DIM1+H1m2+H1p2; ++ii)
    for (int jj=0; jj<DIM2+H2m2+H2p2; ++jj) {
      for (int kk=0; kk<DIM3+H3m2+H3p2; ++kk) {
        b(ii,jj,kk) = triple_t<USE_DOUBLE, T2>();                                      
      }
    }

  for (int ii=0; ii<DIM1+H1m3+H1p3; ++ii)
    for (int jj=0; jj<DIM2+H2m3+H2p3; ++jj) {
      for (int kk=0; kk<DIM3+H3m3+H3p3; ++kk) {
        c(ii,jj,kk) = triple_t<USE_DOUBLE, T3>();
      }
    }

  for (int ii=H1m1; ii<DIM1+H1m1; ++ii)
    for (int jj=H2m1; jj<DIM2+H2m1; ++jj) 
      for (int kk=H3m1; kk<DIM3+H3m1; ++kk) {
        a(ii,jj,kk) = 
          triple_t<USE_DOUBLE, T1>(ii-H1m1+(DIM1)*coords[0],
                   jj-H2m1+(DIM2)*coords[1],
                   kk-H3m1+(DIM3)*coords[2]);
      }

  for (int ii=H1m2; ii<DIM1+H1m2; ++ii)
    for (int jj=H2m2; jj<DIM2+H2m2; ++jj) 
      for (int kk=H3m2; kk<DIM3+H3m2; ++kk) {
        b(ii,jj,kk) = 
          triple_t<USE_DOUBLE, T2>(ii-H1m2+(DIM1)*coords[0]+B_ADD,
                   jj-H2m2+(DIM2)*coords[1]+B_ADD,
                   kk-H3m2+(DIM3)*coords[2]+B_ADD);
      }

  for (int ii=H1m3; ii<DIM1+H1m3; ++ii)
    for (int jj=H2m3; jj<DIM2+H2m3; ++jj) 
      for (int kk=H3m3; kk<DIM3+H3m3; ++kk) {
        c(ii,jj,kk) = 
          triple_t<USE_DOUBLE, T3>(ii-H1m3+(DIM1)*coords[0]+C_ADD,
                   jj-H2m3+(DIM2)*coords[1]+C_ADD,
                   kk-H3m3+(DIM3)*coords[2]+C_ADD);
      }

  file << "A \n";
  printbuff(file,a, DIM1+H1m1+H1p1, DIM2+H2m1+H2p1, DIM3+H3m1+H3p1);
  file << "B \n";
  printbuff(file,b, DIM1+H1m2+H1p2, DIM2+H2m2+H2p2, DIM3+H3m2+H3p2);
  file << "C \n";
  printbuff(file,c, DIM1+H1m3+H1p3, DIM2+H2m3+H2p3, DIM3+H3m3+H3p3);
  file.flush();


#ifdef VECTOR_INTERFACE

  std::vector<GCL::field_on_the_fly<triple_t<USE_DOUBLE, T1>::data_type, layoutmap, pattern_type::traits> > vect(3);

  vect[0] = field1;
  vect[1] = field2;
  vect[2] = field3;

  MPI_Barrier(MPI_COMM_WORLD);

  gettimeofday(&start_tv, NULL);
  he.pack(vect);

  gettimeofday(&stop1_tv, NULL);
  he.exchange();

  gettimeofday(&stop2_tv, NULL);
  he.unpack(vect);

  gettimeofday(&stop3_tv, NULL);
#else
  MPI_Barrier(MPI_COMM_WORLD);

  gettimeofday(&start_tv, NULL);
  he.pack(field1, field2, field3);

  gettimeofday(&stop1_tv, NULL);
  he.exchange();

  gettimeofday(&stop2_tv, NULL);
  he.unpack(field1, field2, field3);

  gettimeofday(&stop3_tv, NULL);
#endif

  lapse_time1 = ((static_cast<double>(stop1_tv.tv_sec)+1/1000000.0*static_cast<double>(stop1_tv.tv_usec)) - (static_cast<double>(start_tv.tv_sec)+1/1000000.0*static_cast<double>(start_tv.tv_usec))) * 1000.0;

  lapse_time2 = ((static_cast<double>(stop2_tv.tv_sec)+1/1000000.0*static_cast<double>(stop2_tv.tv_usec)) - (static_cast<double>(stop1_tv.tv_sec)+1/1000000.0*static_cast<double>(stop1_tv.tv_usec))) * 1000.0;

  lapse_time3 = ((static_cast<double>(stop3_tv.tv_sec)+1/1000000.0*static_cast<double>(stop3_tv.tv_usec)) - (static_cast<double>(stop2_tv.tv_sec)+1/1000000.0*static_cast<double>(stop2_tv.tv_usec))) * 1000.0;

  lapse_time4 = ((static_cast<double>(stop3_tv.tv_sec)+1/1000000.0*static_cast<double>(stop3_tv.tv_usec)) - (static_cast<double>(start_tv.tv_sec)+1/1000000.0*static_cast<double>(start_tv.tv_usec))) * 1000.0;

  MPI_Barrier(MPI_COMM_WORLD);
  file << "TIME PACK: " << lapse_time1 << std::endl;
  file << "TIME EXCH: " << lapse_time2 << std::endl;
  file << "TIME UNPK: " << lapse_time3 << std::endl;
  file << "TIME ALL : " << lapse_time1+lapse_time2+lapse_time3 << std::endl;
  file << "TIME TOT : " << lapse_time4 << std::endl;

  file << "\n********************************************************************************\n";

  file << "A \n";
  printbuff(file,a, DIM1+H1m1+H1p1, DIM2+H2m1+H2p1, DIM3+H3m1+H3p1);
  file << "B \n";
  printbuff(file,b, DIM1+H1m2+H1p2, DIM2+H2m2+H2p2, DIM3+H3m2+H3p2);
  file << "C \n";
  printbuff(file,c, DIM1+H1m3+H1p3, DIM2+H2m3+H2p3, DIM3+H3m3+H3p3);
  file.flush();

  int passed = true;


  /* Checking the data arrived correctly in the whole region
   */
  for (int ii=0; ii<DIM1+H1m1+H1p1; ++ii)
    for (int jj=0; jj<DIM2+H2m1+H2p1; ++jj)
      for (int kk=0; kk<DIM3+H3m1+H3p1; ++kk) {

        triple_t<USE_DOUBLE, T1> ta;
        int tax, tay, taz;

        tax = modulus(ii-H1m1+(DIM1)*coords[0], DIM1*dims[0]);
 
        tay = modulus(jj-H2m1+(DIM2)*coords[1], DIM2*dims[1]);
 
        taz = modulus(kk-H3m1+(DIM3)*coords[2], DIM3*dims[2]);
 
        if (!per0) {
          if ( ((coords[0]==0) && (ii<H1m1)) || 
               ((coords[0] == dims[0]-1) && (ii >= DIM1+H1m1)) ) {
            tax=triple_t<USE_DOUBLE, T1>().x();
           }
        }

        if (!per1) {
          if ( ((coords[1]==0) && (jj<H2m1)) || 
               ((coords[1] == dims[1]-1) && (jj >= DIM2+H2m1)) ) {
            tay=triple_t<USE_DOUBLE, T1>().y();
           }
        }

        if (!per2) {
          if ( ((coords[2]==0) && (kk<H3m1)) || 
               ((coords[2] == dims[2]-1) && (kk >= DIM3+H3m1)) ) {
            taz=triple_t<USE_DOUBLE, T1>().z();
          }
        }

        ta = triple_t<USE_DOUBLE, T1>(tax, tay, taz).floor();

        if (a(ii,jj,kk) != ta) {
          passed = false;
          file << ii << ", " << jj << ", " << kk << " values found != expct: " 
               << "a " << a(ii,jj,kk) << " != " 
               << ta
               << "\n";
        }
      }

  for (int ii=0; ii<DIM1+H1m2+H1p2; ++ii)
    for (int jj=0; jj<DIM2+H2m2+H2p2; ++jj)
      for (int kk=0; kk<DIM3+H3m2+H3p2; ++kk) {

        triple_t<USE_DOUBLE, T2> tb;
        int tbx, tby, tbz;

        tbx = modulus(ii-H1m2+(DIM1)*coords[0], DIM1*dims[0])+B_ADD;

        tby = modulus(jj-H2m2+(DIM2)*coords[1], DIM2*dims[1])+B_ADD;

        tbz = modulus(kk-H3m2+(DIM3)*coords[2], DIM3*dims[2])+B_ADD;

        if (!per0) {
          if ( ((coords[0]==0) && (ii<H1m2)) || 
               ((coords[0] == dims[0]-1) && (ii >= DIM1+H1m2)) ) {
            tbx=triple_t<USE_DOUBLE, T2>().x();
          }
        }

        if (!per1) {
          if ( ((coords[1]==0) && (jj<H2m2)) || 
               ((coords[1] == dims[1]-1) && (jj >= DIM2+H2m2)) ) {
            tby=triple_t<USE_DOUBLE, T2>().y();
          }
        }

        if (!per2) {
          if ( ((coords[2]==0) && (kk<H3m2)) || 
               ((coords[2] == dims[2]-1) && (kk >= DIM3+H3m2)) ) {
            tbz=triple_t<USE_DOUBLE, T2>().z();
          }
        }

        tb = triple_t<USE_DOUBLE, T2>(tbx, tby, tbz).floor();

        if (b(ii,jj,kk) != tb) {
          passed = false;
          file << ii << ", " << jj << ", " << kk << " values found != expct: " 
               << "b " << b(ii,jj,kk) << " != " 
               << tb
               << "\n";
        }
      }

  for (int ii=0; ii<DIM1+H1m3+H1p3; ++ii)
    for (int jj=0; jj<DIM2+H2m3+H2p3; ++jj)
      for (int kk=0; kk<DIM3+H3m3+H3p3; ++kk) {

        triple_t<USE_DOUBLE, T3> tc;
        int tcx, tcy, tcz;

        tcx = modulus(ii-H1m3+(DIM1)*coords[0], DIM1*dims[0])+C_ADD;

        tcy = modulus(jj-H2m3+(DIM2)*coords[1], DIM2*dims[1])+C_ADD;

        tcz = modulus(kk-H3m3+(DIM3)*coords[2], DIM3*dims[2])+C_ADD;

        if (!per0) {
          if ( ((coords[0]==0) && (ii<H1m3)) || 
               ((coords[0] == dims[0]-1) && (ii >= DIM1+H1m3)) ) {
            tcx=triple_t<USE_DOUBLE, T3>().x();
          }
        }

        if (!per1) {
          if ( ((coords[1]==0) && (jj<H2m3)) || 
               ((coords[1] == dims[1]-1) && (jj >= DIM2+H2m3)) ) {
            tcy=triple_t<USE_DOUBLE, T3>().y();
          }
        }

        if (!per2) {
          if ( ((coords[2]==0) && (kk<H3m3)) || 
               ((coords[2] == dims[2]-1) && (kk >= DIM3+H3m3)) ) {
            tcz=triple_t<USE_DOUBLE, T3>().z();
          }
        }

        tc = triple_t<USE_DOUBLE, T3>(tcx, tcy, tcz).floor();

        if (c(ii,jj,kk) != tc) {
          passed = false;
          file << ii << ", " << jj << ", " << kk << " values found != expct: " 
               << "c " << c(ii,jj,kk) << " != " 
               << tc
               << "\n";
        }
      }

  if (passed)
    file << "RESULT: PASSED!\n";
  else
    file << "RESULT: FAILED!\n";
}

int main(int argc, char** argv) {

  /* this example is based on MPI Cart Communicators, so we need to
  initialize MPI. This can be done by GCL automatically
  */
  MPI_Init(&argc, &argv);


  /* Now let us initialize GCL itself. If MPI is not initialized at
     this point, it will initialize it
   */
  GCL::GCL_Init(argc, argv);

  /* Here we compute the computing gris as in many applications
   */
  MPI_Comm_rank(MPI_COMM_WORLD, &pid);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);



  std::cout << pid << " " << nprocs << "\n";

  std::stringstream ss;
  ss << pid;

  std::string filename = "out" + ss.str() + ".txt";

  std::cout << filename << std::endl;
  std::ofstream file(filename.c_str());

  file << pid << "  " << nprocs << "\n";

  MPI_Dims_create(nprocs, 3, dims);
  int period[3] = {1, 1, 1};

  file << "@" << pid << "@ MPI GRID SIZE " << dims[0] << " - " << dims[1] << " - " << dims[2] << "\n";
 
  MPI_Cart_create(MPI_COMM_WORLD, 3, dims, period, false, &CartComm);

  MPI_Cart_get(CartComm, 3, dims, period, coords);


  /* Each process will hold a tile of size
     (DIM1+2*H)x(DIM2+2*H)x(DIM3+2*H). The DIM1xDIM2xDIM3 area inside
     the H width border is the inner region of an hypothetical stencil
     computation whise halo width is H.
   */
  int DIM1=atoi(argv[1]);
  int DIM2=atoi(argv[2]);
  int DIM3=atoi(argv[3]);
  int H1m1  =atoi(argv[4]);
  int H1p1  =atoi(argv[5]);
  int H2m1  =atoi(argv[6]);
  int H2p1  =atoi(argv[7]);
  int H3m1  =atoi(argv[8]);
  int H3p1  =atoi(argv[9]);
  int H1m2  =atoi(argv[10]);
  int H1p2  =atoi(argv[11]);
  int H2m2  =atoi(argv[12]);
  int H2p2  =atoi(argv[13]);
  int H3m2  =atoi(argv[14]);
  int H3p2  =atoi(argv[15]);
  int H1m3  =atoi(argv[16]);
  int H1p3  =atoi(argv[17]);
  int H2m3  =atoi(argv[18]);
  int H2p3  =atoi(argv[19]);
  int H3m3  =atoi(argv[20]);
  int H3p3  =atoi(argv[21]);

  file << "Field A " 
       << "size = " << DIM1 << "x" << DIM2 << "x" << DIM3 << " "
       << "Halo along i " << H1m1 << " - " << H1p1 << ", "
       << "Halo along j " << H2m1 << " - " << H2p1 << ", "
       << "Halo along k " << H3m1 << " - " << H3p1 << std::endl;

  file << "Field B " 
       << "size = " << DIM1 << "x" << DIM2 << "x" << DIM3 << " "
       << "Halo along i " << H1m2 << " - " << H1p2 << ", "
       << "Halo along j " << H2m2 << " - " << H2p2 << ", "
       << "Halo along k " << H3m2 << " - " << H3p2 << std::endl;

  file << "Field C " 
       << "size = " << DIM1 << "x" << DIM2 << "x" << DIM3 << " "
       << "Halo along i " << H1m3 << " - " << H1p3 << ", "
       << "Halo along j " << H2m3 << " - " << H2p3 << ", "
       << "Halo along k " << H3m3 << " - " << H3p3 << std::endl;
  file.flush();

  /* This example will exchange 3 data arrays at the same time with
     different values.
   */
  triple_t<USE_DOUBLE, T1> *_a = new triple_t<USE_DOUBLE, T1>[(DIM1+H1m1+H1p1)*(DIM2+H2m1+H2p1)*(DIM3+H3m1+H3p1)];
  triple_t<USE_DOUBLE, T2> *_b = new triple_t<USE_DOUBLE, T2>[(DIM1+H1m2+H1p2)*(DIM2+H2m2+H2p2)*(DIM3+H3m2+H3p2)];
  triple_t<USE_DOUBLE, T3> *_c = new triple_t<USE_DOUBLE, T3>[(DIM1+H1m3+H1p3)*(DIM2+H2m3+H2p3)*(DIM3+H3m3+H3p3)];


  file << "Permutation 0,1,2\n";

  file << "run<std::ostream, 0,1,2, true, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,1,2, true, true, true>(file, DIM1, DIM2, DIM3,
                                             H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                             H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                             H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                             _a, _b, _c);

  file << "run<std::ostream, 0,1,2, true, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,1,2, true, true, false>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 0,1,2, true, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,1,2, true, false, true>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 0,1,2, true, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,1,2, true, false, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 0,1,2, false, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,1,2, false, true, true>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 0,1,2, false, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,1,2, false, true, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 0,1,2, false, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,1,2, false, false, true>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 0,1,2, false, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,1,2, false, false, false>(file, DIM1, DIM2, DIM3,
                                                H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                                H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                                H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                                _a, _b, _c);
  file << "---------------------------------------------------\n";


  file << "Permutation 0,2,1\n";

  file << "run<std::ostream, 0,2,1, true, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,2,1, true, true, true>(file, DIM1, DIM2, DIM3,
                                             H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                             H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                             H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                             _a, _b, _c);

  file << "run<std::ostream, 0,2,1, true, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,2,1, true, true, false>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 0,2,1, true, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,2,1, true, false, true>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 0,2,1, true, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,2,1, true, false, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 0,2,1, false, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,2,1, false, true, true>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 0,2,1, false, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,2,1, false, true, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 0,2,1, false, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,2,1, false, false, true>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 0,2,1, false, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 0,2,1, false, false, false>(file, DIM1, DIM2, DIM3,
                                                H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                                H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                                H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                                _a, _b, _c);
  file << "---------------------------------------------------\n";


  file << "Permutation 1,0,2\n";

  file << "run<std::ostream, 1,0,2, true, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,0,2, true, true, true>(file, DIM1, DIM2, DIM3,
                                             H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                             H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                             H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                             _a, _b, _c);

  file << "run<std::ostream, 1,0,2, true, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,0,2, true, true, false>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 1,0,2, true, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,0,2, true, false, true>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 1,0,2, true, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,0,2, true, false, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 1,0,2, false, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,0,2, false, true, true>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 1,0,2, false, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,0,2, false, true, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 1,0,2, false, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,0,2, false, false, true>(file, DIM1, DIM2, DIM3, 
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 1,0,2, false, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,0,2, false, false, false>(file, DIM1, DIM2, DIM3, 
                                                H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                                H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                                H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                                _a, _b, _c);
  file << "---------------------------------------------------\n";


  file << "Permutation 1,2,0\n";

  file << "run<std::ostream, 1,2,0, true, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,2,0, true, true, true>(file, DIM1, DIM2, DIM3, H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                             H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                             H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                             _a, _b, _c);

  file << "run<std::ostream, 1,2,0, true, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,2,0, true, true, false>(file, DIM1, DIM2, DIM3, 
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                              _a, _b, _c);

  file << "run<std::ostream, 1,2,0, true, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,2,0, true, false, true>(file, DIM1, DIM2, DIM3, 
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                              _a, _b, _c);

  file << "run<std::ostream, 1,2,0, true, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,2,0, true, false, false>(file, DIM1, DIM2, DIM3, 
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                               _a, _b, _c);

  file << "run<std::ostream, 1,2,0, false, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,2,0, false, true, true>(file, DIM1, DIM2, DIM3, H1m1, H1p1, 
                                              H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                              _a, _b, _c);

  file << "run<std::ostream, 1,2,0, false, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,2,0, false, true, false>(file, DIM1, DIM2, DIM3, H1m1, H1p1, 
                                               H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                               _a, _b, _c);

  file << "run<std::ostream, 1,2,0, false, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 1,2,0, false, false, true>(file, DIM1, DIM2, DIM3, 
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                               _a, _b, _c);

  file << "run<std::ostream, 1,2,0, false, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H31, _a, _b, _c)\n";
  run<std::ostream, 1,2,0, false, false, false>(file, DIM1, DIM2, DIM3, 
                                                H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                                H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                                H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                                _a, _b, _c);
  file << "---------------------------------------------------\n";


  file << "Permutation 2,0,1\n";

  file << "run<std::ostream, 2,0,1, true, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,0,1, true, true, true>(file, DIM1, DIM2, DIM3, 
                                             H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                             H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                             H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                             _a, _b, _c);

  file << "run<std::ostream, 2,0,1, true, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,0,1, true, true, false>(file, DIM1, DIM2, DIM3, 
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                              _a, _b, _c);

  file << "run<std::ostream, 2,0,1, true, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,0,1, true, false, true>(file, DIM1, DIM2, DIM3, 
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                              _a, _b, _c);

  file << "run<std::ostream, 2,0,1, true, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,0,1, true, false, false>(file, DIM1, DIM2, DIM3, 
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                               _a, _b, _c);

  file << "run<std::ostream, 2,0,1, false, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,0,1, false, true, true>(file, DIM1, DIM2, DIM3, 
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                              _a, _b, _c);

  file << "run<std::ostream, 2,0,1, false, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,0,1, false, true, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
 
                                               _a, _b, _c);

  file << "run<std::ostream, 2,0,1, false, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,0,1, false, false, true>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 2,0,1, false, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,0,1, false, false, false>(file, DIM1, DIM2, DIM3,
                                                H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                                H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                                H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
 
                                                _a, _b, _c);
  file << "---------------------------------------------------\n";


  file << "Permutation 2,1,0\n";

  file << "run<std::ostream, 2,1,0, true, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,1,0, true, true, true>(file, DIM1, DIM2, DIM3,
                                             H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                             H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                             H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                             _a, _b, _c);

  file << "run<std::ostream, 2,1,0, true, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,1,0, true, true, false>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 2,1,0, true, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,1,0, true, false, true>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                              _a, _b, _c);

  file << "run<std::ostream, 2,1,0, true, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,1,0, true, false, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                               _a, _b, _c);

  file << "run<std::ostream, 2,1,0, false, true, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,1,0, false, true, true>(file, DIM1, DIM2, DIM3,
                                              H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                              H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                              H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                              _a, _b, _c);

  file << "run<std::ostream, 2,1,0, false, true, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,1,0, false, true, false>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                               _a, _b, _c);

  file << "run<std::ostream, 2,1,0, false, false, true>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,1,0, false, false, true>(file, DIM1, DIM2, DIM3,
                                               H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                               H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                               H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,
                                               _a, _b, _c);

  file << "run<std::ostream, 2,1,0, false, false, false>(file, DIM1, DIM2, DIM3, H1m, H1p, H2m, H2p, H3m, H3p, _a, _b, _c)\n";
  run<std::ostream, 2,1,0, false, false, false>(file, DIM1, DIM2, DIM3,
                                                H1m1, H1p1, H2m1, H2p1, H3m1, H3p1,
                                                H1m2, H1p2, H2m2, H2p2, H3m2, H3p2,
                                                H1m3, H1p3, H2m3, H2p3, H3m3, H3p3,

                                                _a, _b, _c);
  file << "---------------------------------------------------\n";

  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Finalize();
  return 0;
}
