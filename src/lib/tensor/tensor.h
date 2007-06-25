/*
  This file is part of MADNESS.
  
  Copyright (C) <2007> <Oak Ridge National Laboratory>
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
  
  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov 
  tel:   865-241-3937
  fax:   865-572-0680

  
  $Id$
*/

  
#ifndef TENSORH
#define TENSORH

/// \file tensor.h
/// \brief Declares and partially implements Tensor and SliceTensor.

// This is the only file the application need include for
// all tensor functionality.

#include <complex>
#include <vector>
#include <cmath>
#include <cstdlib>

#include <madness_config.h>
#include <world/sharedptr.h>
#include <world/archive.h>

typedef std::complex<float> float_complex;
typedef std::complex<double> double_complex;

// These probably have to be included in this order
#include <tensor/tensor_macros.h>
#include <tensor/type_data.h>
#include <tensor/slice.h>
#include <tensor/vector_factory.h>
#include <tensor/basetensor.h>
#include <tensor/tensoriter.h>
#include <tensor/tensorexcept.h>

#if !HAVE_STD_ABS_LONG
#if !HAVE_STD_LABS
namespace std {
    static inline long abs(long a) {
        return a>=0 ? a : -a;
    }
}
#else
namespace std {
    static inline long abs(long a) {
        return std::labs(a);
    }
#endif
#endif

namespace madness {

    /// A templated tensor or multidimensional array of numeric quantities.

    /// A tensor provides a multidimensional view of numeric data.  It is
    /// only a multi-dimensional array and does not incorporate any ideas
    /// of covariance and contravariance.
    ///
    /// When a new tensor is created, the underlying data is also allocated.
    /// E.g.,
    /// \code
    /// Tensor<double> a(3,4,5)
    /// \endcode
    /// creates a new 3-dimensional tensor and allocates a contiguous
    /// block of 60 doubles which are initialized to zero.  The dimensions
    /// (numbered from the left starting at 0) are in C or row-major
    /// order.  Thus, for the tensor \c a , the stride between successive
    /// elements of the right-most dimension is 1.  For the middle
    /// dimension it is 5.  For the left-most dimension it is 20.  Thus,
    /// the loops
    /// \code
    /// for (i=0; i<3; i++)
    ///   for (j=0; j<4; j++)
    ///     for (k=0; k<5; k++)
    ///       a(i,j,k) = ...
    /// \endcode
    /// will go sequentially (and thus efficiently) through memory.
    /// If the dimensions have been reordered (e.g., with \c swapdim
    /// or \c map), or if the tensor is actually a slice of another
    /// tensor, then the layout in memory may be more complex and
    /// may not reflect a contiguous block of memory.
    ///
    /// Multiple tensors may be used to provide multiple identical or
    /// distinct views of the same data.  E.g., in the following
    /// \code
    /// Tensor<double> a(2,3);  // A new tensor initialized to zero
    /// Tensor<double> b = a;
    /// \endcode
    /// \c a and \c b provide identical views of the same data, thus
    /// \code
    /// b(1,2) = 99;
    /// cout << a(1,2) << endl;  // Outputs 99
    /// cout << b(1,2) << endl;  // Outputs 99
    /// \endcode
    ///
    /// Different views can be generated by using many class methods.
    /// E.g., swapping dimensions.
    /// \code
    /// Tensor<double> a(2,3);  // A new tensor initialized to zero
    /// Tensor<double> b = a.swapdim(0,1);
    /// b(1,2) = 99;
    /// cout << a(2,1) << endl;  // Outputs 99
    /// cout << b(1,2) << endl;  // Outputs 99
    /// \endcode
    ///
    /// It is important to appreciate that the views and the data are
    /// quite independent.  In particular, the default copy constructor
    /// and assignment operations only copy the tensor (the view) and not
    /// the data --- i.e., the copy constructor and assigment operations
    /// only take shallow copies.  This is for both consistency and
    /// efficiency (see below for more details).  For assignment it is
    /// exactly equivalent to the previous behavior of the Tensor class in
    /// Python.  Thus, assigning one tensor to another generates another
    /// view of the same data, replacing any previous view and not moving
    /// or copying any of the data.  The copy constructor \em differs from
    /// the current Python behavior (in Python it is a deep copy) which is
    /// now thought to be incorrect --- the Python behaviour will be
    /// changed to be consistent with that of C++.
    /// E.g.,
    /// \code
    /// Tensor<double> a(2,3);   // A new tensor initialized to zero
    /// Tensor<double> c(3,3,3); // Another new tensor
    /// Tensor<double> b = a;    // b is a view of the same data as a
    /// a = c;                   // a is now a view of c's data
    /// b = c                    // b is now also a view of c's data and the
    ///                          // data allocated originally for a is freed
    /// \endcode
    /// The above example also illustrates how reference counting is used
    /// to keep track of the underlying data.  Once there are no views
    /// of the data, it is automatically freed.
    ///
    /// There are only two ways to actually copy the underlying data.  A
    /// new, complete, and contigous copy of a tensor and its data may be
    /// generated with the copy function.  Or, to copy data from one tensor
    /// into the data viewed by another tensor, you must use a slice.
    ///
    /// Slices generate sub-tensors --- i.e., views of patches of the
    /// data.  E.g., to refer to all but the first and last elements in
    /// each dimension of a matrix
    /// \code a(Slice(1,-2),Slice(1,-2))
    /// \endcode
    /// Or to view odd elements in each dimension
    /// \code
    /// a(Slice(0,-1,2),Slice(0,-1,2))
    /// \endcode
    /// A slice or patch of a
    /// tensor behaves exactly like a tensor \em except for assignment.
    /// When a slice is assigned to, the data is copied with the
    /// requirement that the source and destinations agree in size and
    /// shape (i.e., they conform).  Thus, to copy the all of the data
    /// from a to b,
    /// \code
    /// Tensor<double> a(3,4), b(3,4);
    /// a = 1;                              // Set all elements of a to 1
    /// b = 2;                              // Set all elements of b to 2
    /// a(Slice(0,-1,1),Slice(0,-1,1)) = b; // Copy all data from b to a
    /// a(_,_) = b(_,_);                    // Copy all data from b to a
    /// a(___) = b(___);                    // Copy all data from b to a
    /// a(Slice(1,2),Slice(1,2) = b;        // Error, do not conform
    /// \endcode
    /// Special slice values \c _ ,\c  _reverse, and \c  ___ have
    /// been defined to refer to all elements in a dimension, all
    /// elements in a dimension but reversed, and all elements in all
    /// dimensions, respectively.
    ///
    /// One dimensional tensors (i.e., vectors) may be indexed using
    /// either square brackets (e.g., \c v[i] ) or round brackets (e.g.,
    /// \c v(i) ).  All higher-dimensional tensors must use only round
    /// brackets (e.g., \c t(i,j,k) ).  This is due to C++'s restriction
    /// that the indexing operator (\c [] ) can only have one argument.
    /// The indexing operation should generate efficient inline code.
    ///
    /// For the sake of efficiency, no bounds checking is performed by
    /// default by most single element indexing operations.  Checking can
    /// be enabled at compile time by defining \c -DBOUNDS_CHECKING for
    /// applicaiton files including \c tensor.h.  The general indexing
    /// operation that takes a \c std::vector<long> index and all slicing
    /// operations always perform bounds checking.  To make indexing with
    /// checking a bit easier, a factory function has been provided for
    /// vectors ... but note you need to explicitly use longs as the
    /// index.
    /// \code
    /// Tensor<long> a(7,7,7);
    ///  a(3,4,5) += 1;                   // OK ... adds 1 to element (3,4,5)
    /// a(3,4,9) += 1;                    // BAD ... undetected out-of-bounds access
    /// a(vector_factory(3L,4L,9L)) += 1; // OK ... out-bounds access will
    ///                                   // be detected at runtime.
    /// \endcode
    ///
    /// See \c tensor_macros.h for documentation on iteration over
    /// elements of tensors and tips for optimization.
    ///
    /// Why is the copy constructor shallow?  If the copy constructor was
    /// deep (i.e., copied both the view and the data), then it would not
    /// be possible to return a shallow copy without relying upon the
    /// return value optimization (Google can explain this).  Having the
    /// semantics depend upon an optimization is clearly not correct.  One
    /// difference between Python and C++ is that Python does not
    /// automatically invoke the copy constructor whereas C++ does this in
    /// many different circumstances (e.g., when returning a function
    /// value if the return value optimization cannot be applied, or in
    /// managing compiler generated temporary variables). In Python we
    /// only used the copy constructor to force a deep and contiguous
    /// copy.  Even for Python native container types, the copy constructor
    /// is not deep.

//     // Define complex*real operation but only for stuff in madness namespace
//     template <typename T>
//     inline
//     std::complex<T> operator*(const std::complex<T>& c, const T t) {
//         return std::complex<T>(std::real(c)*t,std::imag(c)*t);
//     };

    template <class T> class Tensor : public BaseTensor {
        // Templated member functions should be defined in the header to
        // avoid instantiation problems.  Specializations need to be
        // declared above here.
    private:
        void init(long nd, const long d[], bool dozero=true);
        SharedArray<T> p;

    protected:
        T* pointer; // SliceTensor needs access

        void internal_shallow_copy(const Tensor<T>& t);
    public:
        /// C++ typename of this tensor.
        typedef T type;

        /// C++ typename of the real type associated with a complex type.
        typedef typename TensorTypeData<T>::scalar_type scalar_type;

        /// C++ typename of the floating point type associated with scalar real type
        typedef typename TensorTypeData<T>::float_scalar_type float_scalar_type;

        /// Default constructor does not allocate any data and sets ndim=-1, size=0, pointer=0, and id.
        inline Tensor() {
            ndim=-1;
            size=0;
            pointer=0;
            id = TensorTypeData<T>::id;
        };

        /// Create and zero new 1-d tensor
        explicit Tensor(int d0);

        /// Create and zero new 1-d tensor
        explicit Tensor(long d0);

        /// Create and zero new 2-d tensor
        Tensor(long d0, long d1);

        /// Create and zero new 3-d tensor
        Tensor(long d0, long d1, long d2);

        /// Create and zero new 4-d tensor
        Tensor(long d0, long d1, long d2, long d3);

        /// Create and zero new 5-d tensor
        Tensor(long d0, long d1, long d2, long d3, long d4);

        /// Create and zero new 6-d tensor
        Tensor(long d0, long d1, long d2, long d3, long d4, long d5);

        /// Create and optionally zero new n-d tensor. This is the most general constructor.
        inline Tensor(const std::vector<long>& d, bool dozero=true) {
            init(d.size(),&(d[0]),dozero);
        };

        /// Politically incorrect general constructor.
        inline Tensor(long nd, const long d[], bool dozero=true) {
            init(nd,d,dozero);
        };

        /// Copy constructor is \em shallow and is identical to assignment.
        inline Tensor(const Tensor<T>& t) {
            internal_shallow_copy(t);
        };

        /// Type conversion implies deep copy.
        template <class Q> operator Tensor<Q>() const { // type conv => deep copy
            Tensor<Q> result = Tensor<Q>(this->ndim,this->dim,false);
            BINARY_OPTIMIZED_ITERATOR(Q, result, T, (*this), *_p0 = (Q)(*_p1));
            return result;
        }

        /// Assignment is a \em shallow copy.
        inline Tensor<T>& operator=(const Tensor<T>& t) {
            if (&t != this) internal_shallow_copy(t);
            return *this;
        };

        /// Tensor = scalar of same type --- fills the tensor with the scalar value.
        Tensor<T>& operator=(T x);

        /// Add two tensors of the same type to produce a new tensor
        Tensor<T> operator+(const Tensor<T>& t) const;

        /// Subtract one tensor from another of the same type to produce a new tensor
        Tensor<T> operator-(const Tensor<T>& t) const;

#if HAVE_NESTED_TEMPLATE_XLC_BUG
        // IBM xlC 6.? does not digest the ISSUPORTED template

        /// Scale tensor by a scalar of other type (IBM XLC only).
        template <typename Q> Tensor<T> operator*(const Q& x) const {
            Tensor<T> result = Tensor<T>(ndim,dim,false);
            BINARY_OPTIMIZED_ITERATOR(T, result, T, (*this), *_p0 = (T) (*_p1 * x));
            return result;
        }

        /// Divide tensor by a scalar of other type (IBM XLC only).
        template <typename Q> Tensor<T> operator/(const Q& x) const {
            Tensor<T> result = Tensor<T>(ndim,dim);
            BINARY_OPTIMIZED_ITERATOR(T, result, T, (*this), *_p0 = (T) (*_p1 / x));
            return result;
        }

        /// Inplace multiply by scalar of other type (IBM XLC only).
        template <typename Q> Tensor<T>& operator*=(const Q& t) {
            UNARY_OPTIMIZED_ITERATOR(T,(*this), *_p0 *= t);
            return *this;
        }
        /// Inplace scaling by scalar of supported type.
        template <typename Q> Tensor<T>& scale(Q x) {
            UNARY_OPTIMIZED_ITERATOR(T, (*this), *_p0 = (T) (*_p0 * x));
            return *this;
        }
#else
        /// Scale tensor by a scalar of a supported type (see type_data.h).
        template ISSUPPORTED(Q,Tensor<T>) operator*(const Q& x) const {
            Tensor<T> result = Tensor<T>(ndim,dim,false);
            BINARY_OPTIMIZED_ITERATOR(T, result, T, (*this), *_p0 = (T) (*_p1 * x));
            return result;
        }

        /// Divide tensor by a scalar of a supported type (see type_data.h).
        template ISSUPPORTED(Q,Tensor<T>) operator/(const Q& x) const {
            Tensor<T> result = Tensor<T>(ndim,dim);
            BINARY_OPTIMIZED_ITERATOR(T, result, T, (*this), *_p0 = (T) (*_p1 / x));
            return result;
        }

        /// Inplace multiply by scalar of supported type (see type_data.h).
        template ISSUPPORTED(Q,Tensor<T>&) operator*=(const Q& t) {
            UNARY_OPTIMIZED_ITERATOR(T,(*this), *_p0 *= t);
            return *this;
        }

        /// Inplace scaling by scalar of supported type.
        template ISSUPPORTED(Q,Tensor<T>&) scale(Q x) {
            UNARY_OPTIMIZED_ITERATOR(T, (*this), *_p0 = (T) (*_p0 * x));
            return *this;
        }
#endif

        /// Add a scalar of the same type to all elements of a tensor producing a new tensor
        Tensor<T> operator+(T x) const;

        /// Unary negation producing a new tensor
        Tensor<T> operator-() const;

        /// Subtract a scalar of the same type from all elements of a tensor producing a new tensor
        Tensor<T> operator-(T x) const;

        /// Inplace add tensor of different type using default C++ type conversions.
        template <class Q> Tensor<T>& operator+=(const Tensor<Q>& t) {
            BINARY_OPTIMIZED_ITERATOR(T, (*this), T, t, *_p0 += *_p1);
            return *this;
        }

        /// Inplace subtract tensor of different type using default C++ type conversions.
        template <class Q> Tensor<T>& operator-=(const Tensor<Q>& t) {
            BINARY_OPTIMIZED_ITERATOR(T, (*this), T, t, *_p0 -= *_p1);
            return *this;
        }

        /// Inplace add a scalar of the same type to all elements
        Tensor<T>& operator+=(T x);

        /// Inplace subtract a scalar of the same type from all elements
        Tensor<T>& operator-=(T x);

        /// Return true if bounds checking was enabled at compile time
        static bool bounds_checking() {
#ifdef BOUNDS_CHECKING
            return true;
#else
            return false;
#endif
        };

        /// 1-d indexing operation using \c [] \em without bounds checking.
        inline T& operator[](long i) const {
#ifdef BOUNDS_CHECKING
            TENSOR_ASSERT(i>=0 && i<dim[0],"1d bounds check failed dim=0",i,this);
#endif
            return pointer[i*stride[0]];
        };

        /// 1-d indexing operation \em without bounds checking.
        inline T& operator()(long i) const {
#ifdef BOUNDS_CHECKING
            TENSOR_ASSERT(i>=0 && i<dim[0],"1d bounds check failed dim=0",i,this);
#endif
            return pointer[i*stride[0]];
        };

        /// 2-d indexing operation \em without bounds checking.
        inline T& operator()(long i, long j) const {
#ifdef BOUNDS_CHECKING
            TENSOR_ASSERT(i>=0 && i<dim[0],"2d bounds check failed dim=0",i,this);
            TENSOR_ASSERT(j>=0 && j<dim[1],"2d bounds check failed dim=1",j,this);
#endif
            return pointer[i*stride[0]+j*stride[1]];
        };

        /// 3-d indexing operation \em without bounds checking.
        inline T& operator()(long i, long j, long k) const {
#ifdef BOUNDS_CHECKING
            TENSOR_ASSERT(i>=0 && i<dim[0],"3d bounds check failed dim=0",i,this);
            TENSOR_ASSERT(j>=0 && j<dim[1],"3d bounds check failed dim=1",j,this);
            TENSOR_ASSERT(k>=0 && k<dim[2],"3d bounds check failed dim=2",k,this);
#endif
            return pointer[i*stride[0]+j*stride[1]+k*stride[2]];
        };

        /// 4-d indexing operation \em without bounds checking.
        inline T& operator()(long i, long j, long k, long l) const {
#ifdef BOUNDS_CHECKING
            TENSOR_ASSERT(i>=0 && i<dim[0],"4d bounds check failed dim=0",i,this);
            TENSOR_ASSERT(j>=0 && j<dim[1],"4d bounds check failed dim=1",j,this);
            TENSOR_ASSERT(k>=0 && k<dim[2],"4d bounds check failed dim=2",k,this);
            TENSOR_ASSERT(l>=0 && l<dim[3],"4d bounds check failed dim=3",l,this);
#endif
            return pointer[i*stride[0]+j*stride[1]+k*stride[2]+
                           l*stride[3]];
        };

        /// 5-d indexing operation \em without bounds checking.
        inline T& operator()(long i, long j, long k, long l, long m) const {
#ifdef BOUNDS_CHECKING
            TENSOR_ASSERT(i>=0 && i<dim[0],"5d bounds check failed dim=0",i,this);
            TENSOR_ASSERT(j>=0 && j<dim[1],"5d bounds check failed dim=1",j,this);
            TENSOR_ASSERT(k>=0 && k<dim[2],"5d bounds check failed dim=2",k,this);
            TENSOR_ASSERT(l>=0 && l<dim[3],"5d bounds check failed dim=3",l,this);
            TENSOR_ASSERT(m>=0 && m<dim[4],"5d bounds check failed dim=4",m,this);
#endif
            return pointer[i*stride[0]+j*stride[1]+k*stride[2]+
                           l*stride[3]+m*stride[4]];
        };

        /// 6-d indexing operation \em without bounds checking.
        inline T& operator()(long i, long j, long k, long l, long m, long n) const {
#ifdef BOUNDS_CHECKING
            TENSOR_ASSERT(i>=0 && i<dim[0],"6d bounds check failed dim=0",i,this);
            TENSOR_ASSERT(j>=0 && j<dim[1],"6d bounds check failed dim=1",j,this);
            TENSOR_ASSERT(k>=0 && k<dim[2],"6d bounds check failed dim=2",k,this);
            TENSOR_ASSERT(l>=0 && l<dim[3],"6d bounds check failed dim=3",l,this);
            TENSOR_ASSERT(m>=0 && m<dim[4],"6d bounds check failed dim=4",m,this);
            TENSOR_ASSERT(n>=0 && n<dim[5],"6d bounds check failed dim=5",n,this);
#endif
            return pointer[i*stride[0]+j*stride[1]+k*stride[2]+
                           l*stride[3]+m*stride[4]+n*stride[5]];
        };

        /// General indexing operation \em with bounds checking.
        T& operator()(const std::vector<long> ind) const;

        /// General slicing operation
        inline SliceTensor<T> operator()(const std::vector<Slice>& s) const {
            return SliceTensor<T>(*this,&(s[0]));
        };

        /// All possible combinations of integer and slice indexing for dimensions 1, 2 and 3.
        SliceTensor<T> operator()(const Slice& s0) const;
        SliceTensor<T> operator()(long i, const Slice& s1) const;
        SliceTensor<T> operator()(const Slice& s0, long j) const;
        SliceTensor<T> operator()(const Slice& s0, const Slice& s1) const;
        SliceTensor<T> operator()(const Slice& s0, const Slice& s1, const Slice& s2) const;
        SliceTensor<T> operator()(long i, const Slice& s1, const Slice& s2) const;
        SliceTensor<T> operator()(const Slice& s0, long j, const Slice& s2) const;
        SliceTensor<T> operator()(const Slice& s0, const Slice& s1, long k) const;
        SliceTensor<T> operator()(long i, long j, const Slice& s2) const;
        SliceTensor<T> operator()(long i, const Slice& s1, long k) const;
        SliceTensor<T> operator()(const Slice& s0, long j, long k) const;

        /// 4-d slicing
        SliceTensor<T> operator()(const Slice& s0, const Slice& s1, const Slice& s2,
                                  const Slice& s3) const;

        /// 5-d slicing
        SliceTensor<T> operator()(const Slice& s0, const Slice& s1, const Slice& s2,
                                  const Slice& s3, const Slice& s4) const;
        /// 6-d slicing
        SliceTensor<T> operator()(const Slice& s0, const Slice& s1, const Slice& s2,
                                  const Slice& s3, const Slice& s4, const Slice& s5) const;

        /// Return a new view reshaping the size and number of dimensions using \c BaseTensor::reshape_base
        Tensor<T> reshape(const std::vector<long>& d) const;

        /// Return a new view reshaping to 1-d of given dimension using \c BaseTensor::reshape_base
        Tensor<T> reshape(long dim0) const;

        /// Return a new view reshaping to 2-d of given dimensions using \c BaseTensor::reshape_base
        Tensor<T> reshape(long dim0, long dim1) const;

        /// Return a new view reshaping to 3-d of given dimensions using \c BaseTensor::reshape_base
        Tensor<T> reshape(long dim0, long dim1, long dim2) const;

        /// Return a new view reshaping to 4-d of given dimensions using \c BaseTensor::reshape_base
        Tensor<T> reshape(long dim0, long dim1, long dim2,
                          long dim3) const;

        /// Return a new view reshaping to 5-d of given dimensions using \c BaseTensor::reshape_base
        Tensor<T> reshape(long dim0, long dim1, long dim2,
                          long dim3, long dim4) const;

        /// Return a new view reshaping to 6-d of given dimensions using \c BaseTensor::reshape_base
        Tensor<T> reshape(long dim0, long dim1, long dim2,
                          long dim3, long dim4, long dim5) const;

        /// Return a new flat (1-d) view using \c BaseTensor::flat_base
        Tensor<T> flat() const;

        /// Return a new view splitting dimension \c i as \c dimi0*dimi1 using \c BaseTensor::splitdim_base
        Tensor<T> splitdim(long i, long dimi0, long dimi1) const;

        /// Return a new view swapping dimensions \c i and \c j using \c BaseTensor::swapdim_base
        Tensor<T> swapdim(long idim, long jdim) const;

        /// Return a new view fusing contiguous dimensions \c i and \c i+1 using \c BaseTensor::fusedim_base
        Tensor<T> fusedim(long i) const;

        /// Return a new view cycling the sub-dimensions (start,...,end) shift steps using \c BaseTensor::cycledim_base
        Tensor<T> cycledim(long shift, long start, long end) const;

        /// Return a new view permuting the dimensions using \c BaseTensor::mapdim_base
        Tensor<T> mapdim(const std::vector<long>& map) const;

        /// Test if \c *this and \c t conform.
        template <class Q> inline bool conforms(const Tensor<Q>& t) const {
            return BaseTensor::conforms(&t);
        }

        /// Fill *this with random values ([0,1] for floats, [0,MAXSIZE] for integers).
        Tensor<T>& fillrandom();

        /// Fill *this with the given scalar
        Tensor<T>& fill(T x);

        /// Fill *this with the index of each element

        /// Each element is assigned it's logical index according to this loop structure
        /// \code
        /// Tensor<float> t(5,6,7,...)
        /// long index=0;
        /// for (long i=0; i<dim[0]; i++)
        ///    for (long j=0; j<dim[1]; j++)
        ///       for (long k=0; k<dim[2]; k++)
        ///          ...
        ///          tensor(i,j,k,...) = index++
        /// \endcode
        Tensor<T>& fillindex();

        /// Set elements of \c *this less than \c x in absolute magnitude to zero.
        Tensor<T>& screen(double x);

        T sum() const;
        T sumsq() const;
        T product() const;
        T min(long *ind = 0) const;
        T max(long *ind = 0) const ;

        // For complex types, this next group returns the appropriate real type
        // For real types, the same type as T is returned (type_data.h)
        float_scalar_type normf() const;
        scalar_type absmin(long *ind = 0) const;
        scalar_type absmax(long *ind = 0) const;

        T trace(const Tensor<T>& t) const;
        Tensor<T>& unaryop(T (*op) (T));
        Tensor<T>& emul(const Tensor<T>& t);
        Tensor<T>& gaxpy(T alpha, const Tensor<T>& t, T beta);

        inline T* ptr() const {
            return pointer;
        };

        /// Return iterator over single tensor
        inline TensorIterator<T> unary_iterator(long iterlevel=0,
                                                bool optimize=true,
                                                bool fusedim=true,
                                                long jdim=default_jdim) const {
            return TensorIterator<T>(this,(const Tensor<T>*) 0, (const Tensor<T>*) 0,
                                     iterlevel, optimize, fusedim, jdim);
        };

        /// Return iterator over two tensors
        template <class Q>
        inline TensorIterator<T,Q> binary_iterator(const Tensor<Q>& q,
                long iterlevel=0,
                bool optimize=true,
                bool fusedim=true,
                long jdim=default_jdim) const {
            return TensorIterator<T,Q>(this,&q,(const Tensor<T>*) 0,
                                       iterlevel, optimize, fusedim, jdim);
        }

        /// Return iterator over three tensors
        template <class Q, class R>
        inline TensorIterator<T,Q,R> ternary_iterator(const Tensor<Q>& q,
                const Tensor<R>& r,
                long iterlevel=0,
                bool optimize=true,
                bool fusedim=true,
                long jdim=default_jdim) const {
            return TensorIterator<T,Q,R>(this,&q,&r,
                                         iterlevel, optimize, fusedim, jdim);
        }

        /// End point for forward iteration
        inline const TensorIterator<T>& end() const {
            static TensorIterator<T> theend(0,0,0,0,0,0);
            return theend;
        };

        /// Helper for generic base functionality ... shallow copy allocated on heap
        inline BaseTensor* new_shallow_copy_base() const {
            return new Tensor<T>(*this);
        };

        /// Helper for generic base functionality ... deep copy allocated on heap
        inline BaseTensor* new_deep_copy_base() const {
            return new_copy(*this);
        };

        /// Helper for generic base functionality ... slice allocated on heap
        inline BaseTensor* slice_base(const std::vector<Slice>& s) const {
            return new SliceTensor<T>(*this,&(s[0]));
        };

        inline BaseTensor* get_base() {
            return (BaseTensor *) this;
        };

        virtual ~Tensor() {};
    };

    template <class T>
    std::ostream& operator << (std::ostream& out, const Tensor<T>& t);


    namespace archive {
        /// Serialize a tensor
        template <class Archive, typename T>
        struct ArchiveStoreImpl< Archive, Tensor<T> > {
            static inline void store(const Archive& s, const Tensor<T>& t) {
                if (t.iscontiguous()) {
                    s & t.size & t.id;
	            if (t.size) s & t.ndim & t.dim & wrap(t.ptr(),t.size);
                }
                else {
                    s & copy(t);
                }
            };
        };
        
        
        /// Deserialize a tensor ... existing tensor is replaced
        template <class Archive, typename T>
        struct ArchiveLoadImpl< Archive, Tensor<T> > {
            static inline void load(const Archive& s, Tensor<T>& t) {
                long sz, id;
                s & sz & id;
                if (id != t.id) throw "type mismatch deserializing a tensor";
                if (sz) {
                    long ndim, dim[TENSOR_MAXDIM];
                    s & ndim & dim;
                    t = Tensor<T>(ndim, dim, false);
                    s & wrap(t.ptr(), t.size);
                }
                else {
                    t = Tensor<T>();
                }
            };
        };
        
//         /// Serialize a Tensor thru a BaseTensor pointer
//         template <class Archive>
//         struct ArchiveStoreImpl< Archive, BaseTensor* > {
//             static inline void store(const Archive& s, const BaseTensor* t) {
//         //		std::cout << "serizialing thru bt\n";
//                 if (t)
//                 {
//                     s & 1;
//         //		    std::cout << "t->id = " << t->id << std::endl;
//                     s & t->id;
//         //		    std::cout << "serialized id" << std::endl;
//                     if (t->id == TensorTypeData<double>::id) {
//         //		        std::cout << "serizialing thru bt ... it's a double!\n";
//                         s & *((const Tensor<double>*) t);
//                     } else {
//                         throw "not yet";
//                     }
//                 }
//                 else
//                 {
//                     s & 0;
//                 }
//             };
//         };
        
//         /// Deserialize a Tensor thru a BaseTensor pointer
        
//         /// It allocates a NEW tensor ... the original point is stomped on
//         /// and so should not be preallocated.
//         template <class Archive>
//         struct ArchiveLoadImpl< Archive, BaseTensor* > {
//             static inline void load(const Archive& s, BaseTensor*& t) {
//                 int loadit;
//                 s & loadit;
//                 if (loadit)
//                 {
//                     long id;
//                     s & id;
//         //		    std::cout << "deserizialing thru bt\n";
//                     if (id == TensorTypeData<double>::id) {
//         //		        std::cout << "deserizialing thru bt ... it's a double!\n";
//                         Tensor<double>* d = new Tensor<double>();
//                         s & *d;
//                         t = d;
//                     } else {
//                         throw "not yet";
//                     }
//                 }
//                 else
//                 {
//         //		    std::cout << "empty tensor" << std::endl;
//                     t = 0;
//                 }
//             };
//         };
    }

#if HAVE_NESTED_TEMPLATE_XLC_BUG
    /// The class defines tensor op scalar ... here define scalar op tensor.
    template <typename T, typename Q>
    inline Tensor<T> operator+(Q x, const Tensor<T>& t) {
        return t+x;
    }

    /// The class defines tensor op scalar ... here define scalar op tensor.
    /// Restrict to supported scalar types or typw disaster occurs.
    template <typename T, typename Q>
    inline Tensor<T> operator*(const Q& x, const Tensor<T>& t) {
        return t*x;
    }

    /// The class defines tensor op scalar ... here define scalar op tensor.
    /// Restrict to supported scalar types or typw disaster occurs.
    template <typename T, typename Q>
    inline Tensor<T> operator-(Q x, const Tensor<T>& t) {
        return (-t)+=x;
    }
#else
    /// The class defines tensor op scalar ... here define scalar op tensor.
    /// Restrict to supported scalar types or typw disaster occurs.
    template <typename T, typename Q>
    inline typename IsSupported < TensorTypeData<Q>, Tensor<T> >::type
    operator+(Q x, const Tensor<T>& t) {
        return t+x;
    }

    /// The class defines tensor op scalar ... here define scalar op tensor.
    /// Restrict to supported scalar types or typw disaster occurs.
    template <typename T, typename Q>
    inline typename IsSupported < TensorTypeData<Q>, Tensor<T> >::type
    operator*(const Q& x, const Tensor<T>& t) {
        return t*x;
    }

    /// The class defines tensor op scalar ... here define scalar op tensor.
    /// Restrict to supported scalar types or typw disaster occurs.
    template <typename T, typename Q>
    inline typename IsSupported < TensorTypeData<Q>, Tensor<T> >::type
    operator-(Q x, const Tensor<T>& t) {
        return (-t)+=x;
    }
#endif

    // Functions that act upon tensors producing new tensors (not views)

    template <class T> Tensor<T> copy(const Tensor<T>& t);

    /// Return a pointer to a new deep copy allocated on the heap
    template <class T>
    Tensor<T>* new_copy(const Tensor<T>& t) {
        return new Tensor<T>(copy(t));
    }

    template <class T>
    Tensor<T> outer(const Tensor<T>& left, const Tensor<T>& right);

    template <class T, class Q>
    Tensor<T> transform(const Tensor<T>& t, const Tensor<Q>& c);

    template <class T>
    void fast_transform(const Tensor<T>& t, const Tensor<T>& c,
                        Tensor<T>& result, Tensor<T>& workspace);

    template <typename T>
    Tensor<T>& transform3d_inplace(Tensor<T>& s, const Tensor<double>& c, Tensor<T>& work);

    template <class T, class Q>
    Tensor<T> inner(const Tensor<T>& left, const Tensor<Q>& right,
                    long k0=-1, long k1=0);

    template <class T, class Q>
    void inner_result(const Tensor<T>& left, const Tensor<Q>& right,
                      long k0, long k1, Tensor<T>& result);

    template <class T>
    Tensor< typename Tensor<T>::scalar_type > abs(const Tensor<T>& t);

    template <class T>
    Tensor< typename Tensor<T>::scalar_type > arg(const Tensor<T>& t);

    template <class T>
    Tensor< typename Tensor<T>::scalar_type > real(const Tensor<T>& t);

    template <class T>
    Tensor< typename Tensor<T>::scalar_type > imag(const Tensor<T>& t);

    template <class T>
    Tensor<T> conj(const Tensor<T>& t);

    /// Returns a new deep copy of the transpose of the input tensor
    template <class T>
    inline Tensor<T> transpose(const Tensor<T>& t) {
        TENSOR_ASSERT(t.ndim == 2, "transpose requires a matrix", t.ndim, &t);
        return copy(t.swapdim(0,1));
    }

    /// Returns a new deep copy of the complex conjugate transpose of the input tensor
    template <class T>
    inline Tensor<T> conj_transpose(const Tensor<T>& t) {
        TENSOR_ASSERT(t.ndim == 2, "conj_transpose requires a matrix", t.ndim, &t);
        return conj(t.swapdim(0,1));
    }

    /// Indexing a tensor with slices returns a slice tensor.

    /// A slice tensor differs from a tensor only in that assignment
    /// causes the data to be copied.  You will usually not instantiate
    /// one except as a temporary produced by indexing a tensor with
    /// slice, performing some operation, and then assigning it back
    /// to a tensor or just discarding.
    template <class T> class SliceTensor:public Tensor<T> {
    private:
        SliceTensor<T>();

    public:
        SliceTensor(const Tensor<T>& t, const Slice s[]);

        template <class Q> SliceTensor<T>& operator=(const SliceTensor<Q>& t) {
            BINARY_OPTIMIZED_ITERATOR(T, (*this), Q, t, *_p0 = (T)(*_p1));
            return *this;
        }

        template <class Q> SliceTensor<T>& operator=(const Tensor<Q>& t) {
            BINARY_OPTIMIZED_ITERATOR(T, (*this), Q, t, *_p0 = (T)(*_p1));
            return *this;
        }

        SliceTensor<T>& operator=(const SliceTensor<T>& t);
        SliceTensor<T>& operator=(const Tensor<T>& t);
        SliceTensor<T>& operator=(T x);
        ~SliceTensor() {};		// Tensor<T> destructor does enough
    };

    template <class T>
    Tensor<T> transform3d(const Tensor<T>& t, const Tensor<T>& c);
    Tensor<double_complex> transform3d(const Tensor<double_complex>& t, const Tensor<double>& c);

    template <class T>
    Tensor<T> transform3d_3c(const Tensor<T>& t,
                             const Tensor<T>& c0, const Tensor<T>& c1, const Tensor<T>& c2);

    /*
    template <typename T> 
    void mTxm(long dimi, long dimj, long dimk,
          T* RESTRICT c, const T* RESTRICT a, const T* RESTRICT b);

    void mTxm(long dimi, long dimj, long dimk,
          double_complex* RESTRICT c, const double_complex* RESTRICT a, const double* RESTRICT b);

    */


    // Specializations for complex types
    template <> Tensor<float_complex>::scalar_type Tensor<float_complex>::normf() const ;
    template <> Tensor<double_complex>::scalar_type Tensor<double_complex>::normf() const ;
    template<> float_complex Tensor<float_complex>::min(long* ind) const ;
    template<> double_complex Tensor<double_complex>::min(long* ind) const ;
    template<> float_complex Tensor<float_complex>::max(long* ind) const ;
    template<> double_complex Tensor<double_complex>::max(long* ind) const ;

}
#endif
