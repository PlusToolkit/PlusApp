#ifndef __PLUSMATH_H
#define __PLUSMATH_H

#include "PlusConfigure.h"
#include <vector>

#include "vnl/vnl_matrix.h"
#include "vnl/vnl_vector.h"
#include "vnl/vnl_sparse_matrix.h"   
#include "vtkMatrix4x4.h"
#include "vtkTransform.h"

class VTK_EXPORT PlusMath
{
public:

  //! Description: 
	// Solve Ax = b sparse linear equations with linear least squares method (vnl_lsqr)
	// The coefficient matrix aMatrix should be m-by-n and the column vector bVector must have length m. 
	// resultVector size need to be fixed by constructor time
  static PlusStatus LSQRMinimize(const std::vector< std::vector<double> > &aMatrix, const std::vector<double> &bVector, vnl_vector<double> &resultVector); 
  static PlusStatus LSQRMinimize(const std::vector<vnl_vector<double>> &aMatrix, const std::vector<double> &bVector, vnl_vector<double> &resultVector); 
  static PlusStatus LSQRMinimize(const vnl_sparse_matrix<double> &sparseMatrixLeftSide, const vnl_vector<double> &vectorRightSide, vnl_vector<double> &resultVector); 

  //! Description: 
  // Returns the Euclidean distance between two 4x4 homogeneous transformation matrix
  static double GetPositionDifference(vtkMatrix4x4* aMatrix, vtkMatrix4x4* bMatrix); 

  //! Description: 
  // Returns the orientation difference in degrees between two 4x4 homogeneous transformation matrix
  static double GetOrientationDifference(vtkMatrix4x4* aMatrix, vtkMatrix4x4* bMatrix); 

  //! Description:
  // Spherical linear interpolation between two rotation quaternions.
  // t is a value between 0 and 1 that interpolates between from and to (t=0 means the results is the same as "from").
  // Precondition: no aliasing problems to worry about ("result" can be "from" or "to" param).
  // Parameters: adjustSign - If true, then slerp will operate by adjusting the sign of the slerp to take shortest path
  // References: From Adv Anim and Rendering Tech. Pg 364
  static void Slerp(double *result, double t, double *from, double *to, bool adjustSign = true); 

  //! Description: 
  // Returns a string containing the parameters (rotation, translation, scaling) from a transformation
  static std::string GetTransformParametersString(vtkTransform* transform);

protected:
  PlusMath(); 
  ~PlusMath();

private: 
  PlusMath(PlusMath const&);
  PlusMath& operator=(PlusMath const&);
};

//****************************************************************************
// ROUNDING CODE
//****************************************************************************

//----------------------------------------------------------------------------
// rounding functions, split and optimized for each type
// (because we don't want to round if the result is a float!)

// in the case of a tie between integers, the larger integer wins.

// The 'floor' function on x86 and mips is many times slower than these
// and is used a lot in this code, optimize for different CPU architectures
// static inline int vtkUltraFloor(double x)
// {
// #if defined mips || defined sparc
//   return (int)((unsigned int)(x + 2147483648.0) - 2147483648U);
// #elif defined i386
//   double tempval = (x - 0.25) + 3377699720527872.0; // (2**51)*1.5
//   return ((int*)&tempval)[0] >> 1;
// #else
//   return int(floor(x));
// #endif
// }

static inline int vtkUltraFloor(double x)
{
#if defined mips || defined sparc || defined __ppc__
  x += 2147483648.0;
  unsigned int i = (unsigned int)(x);
  return (int)(i - 2147483648U);
#elif defined i386 || defined _M_IX86
  union { double d; unsigned short s[4]; unsigned int i[2]; } dual;
  dual.d = x + 103079215104.0;  // (2**(52-16))*1.5
  return (int)((dual.i[1]<<16)|((dual.i[0])>>16));
#elif defined ia64 || defined __ia64__ || defined IA64
  x += 103079215104.0;
  long long i = (long long)(x);
  return (int)(i - 103079215104LL);
#else
  double y = floor(x);
  return (int)(y);
#endif
}

static inline int vtkUltraCeil(double x)
{
  return -vtkUltraFloor(-x - 1.0) - 1;
}

static inline int vtkUltraRound(double x)
{
  return vtkUltraFloor(x + 0.5);
}

static inline int vtkUltraFloor(float x)
{
  return vtkUltraFloor((double)x);
}

static inline int vtkUltraCeil(float x)
{
  return vtkUltraCeil((double)x);
}

static inline int vtkUltraRound(float x)
{
  return vtkUltraRound((double)x);
}

// convert a float into an integer plus a fraction
template <class F>
static inline int vtkUltraFloor(F x, F &f)
{
  int ix = vtkUltraFloor(x);
  f = x - ix;
  return ix;
}

template <class F, class T>
static inline void vtkUltraRound(F val, T& rnd)
{
  rnd = vtkUltraRound(val);
}


#endif 
