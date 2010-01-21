// -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi: set et ts=4 sw=2 sts=2:
#ifndef DUNE_GRIDTYPE_HH
#define DUNE_GRIDTYPE_HH

/**
 * @file
 * @author Andreas Dedner
 *
 * @brief A simple strategy for defining a grid type depending on
 *        defines set during the make process.
 *
 * Based on the make directive <tt> GRIDTYPE=GRIDNAME </tt> a type
 * <tt> GridType </tt> is defined in the namespace Dune::GridSelector
 * specifying a grid implementation. The grids provided in the grid
 * module are possible values:
 * \c ALBERTAGRID, \c ALUGRID_CUBE, \c ALUGRID_SIMPLEX,
 * \c ALUGRID_CONFORM, \c ONEDGRID, \c SGRID, \c UGGRID or \c YASPGRID;
 * but by implementing
 * \code
 * #if defined MYGRID
 *   #if HAVE_GRIDTYPE
 *     #error "Ambiguous definition of GRIDTYPE."
 *   #endif
 *   #include <mygrid.hh>
 *   namespace Dune
 *   {
 *     namespace GridSelector
 *     {
 *       typedef MyGrid< dimgrid, dimworld > GridType;
 *     }
 *   }
 *   #define HAVE_GRIDTYPE 1
 * #endif
 * \endcode
 * the construction can be extended to allow for <tt> GRIDTYPE=MYGRID </tt>.
 * Dimension of grid and world are taken from the dimgrid and dimworld variables
 * defined in the header griddim.hh.
 * If no grid is defined or more than one is defined an error is generated.
 * Also if a combination of <tt> dimgrid, dimeworld </tt> is provided,
 * that is not supported by the selected grid an error is produced.
 *
 * To reduce differences between serial and parallel runs as much as possible,
 * the Dune::MPIHelper class is used to toggle these runs.
 * To use this feature, the following code should always be called at the beginning
 * of the function main:
 * @code
 *#include <dune/grid/utility/gridtype.hh>

   ...

   int main(int argc, char ** argv, char ** envp) {

   // get reference to the singelton MPIHelper
   MPIHelper & mpiHelper = MPIHelper::instance(argc,argv);

   // optional one can get rank and size from this little helper class
   int myrank = mpiHelper.rank();
   int mysize = mpiHelper.size();

   ...
   // construct the grid, see documentation for constructors
   GridType grid;
   ...

   // as the MPIHelper is a singleton, on it's destruction the
   // MPI_Finalize() command is called.
   }
 * @endcode
 *
 * A program that wants to use \ref gridtype.hh should be compiled with
 * either the <tt>ALL_PKG_CPPFLAGS</tt> or the <tt>GRIDDIM_CPPFLAGS</tt>
 * included in its <tt>CPPFLAGS</tt>. This adds
 * @code
 *  -DGRIDDIM=$(GRIDDIM) -DWORLDDIM=$(WORLDDIM) -D$(GRIDTYPE)
 * @endcode
 * to the programs CPPFLAGS. It is then possible to specify the
 * desired grid at make time using something like
 * @code
 *  make GRIDDIM=3 GRIDTYPE=ALBERTAGRID myprogram
 * @endcode
 * It is also possible to provide default settings by using the configure flag
 * <tt>--with-gridtype=...</tt>.
 */

#include <dune/common/deprecated.hh>
#include <dune/grid/utility/griddim.hh>

#if HEADERCHECK
  #undef NOGRID
  #define YASPGRID
#endif

// Check for AlbertaGrid
#if defined ALBERTAGRID
  #if HAVE_GRIDTYPE
    #error "Ambiguous definition of GRIDTYPE."
  #endif
  #if not HAVE_ALBERTA
    #error "ALBERTAGRID defined but no ALBERTA version found!"
  #endif
  #if (GRIDDIM < 1) || (GRIDDIM > 3)
    #error "ALBERTAGRID is only available for GRIDDIM=1, GRIDDIM=2 and GRIDDIM=3."
  #endif

  #include <dune/grid/albertagrid.hh>
namespace Dune
{
  namespace GridSelector
  {
    typedef Dune::AlbertaGrid< dimgrid > GridType;
  }
}
  #define HAVE_GRIDTYPE 1
#endif


// Check ALUGrid
#if defined ALUGRID_CUBE
  #if HAVE_GRIDTYPE
    #error "Ambiguous definition of GRIDTYPE."
  #endif
  #if not HAVE_ALUGRID
    #error "ALUGRID_CUBE defined but no ALUGRID version found!"
  #endif
  #if (GRIDDIM != 3) || (WORLDDIM != GRIDDIM)
    #error "ALUGRID_CUBE is only available for GRIDDIM=3 and WORLDDIM=GRIDDIM."
  #endif

  #include <dune/grid/alugrid.hh>
namespace Dune
{
  namespace GridSelector
  {
    typedef Dune :: ALUCubeGrid< dimgrid, dimworld > GridType;
  }
}
  #define HAVE_GRIDTYPE 1
#endif

#if defined ALUGRID_SIMPLEX
  #if HAVE_GRIDTYPE
    #error "Ambiguous definition of GRIDTYPE."
  #endif
  #if not HAVE_ALUGRID
    #error "ALUGRID_SIMPLEX defined but no ALUGRID version found!"
  #endif
  #if (GRIDDIM < 2) || (GRIDDIM > 3)
    #error "ALUGRID_SIMPLEX is only available for GRIDDIM=2 and GRIDDIM=3."
  #endif
  #if (WORLDDIM != GRIDDIM)
    #error "ALUGRID_SIMPLEX is only available for WORLDDIM=GRIDDIM."
  #endif

  #include <dune/grid/alugrid.hh>
namespace Dune
{
  namespace GridSelector
  {
    typedef Dune :: ALUSimplexGrid< dimgrid, dimworld > GridType;
  }
}
  #define HAVE_GRIDTYPE 1
#endif

#if defined ALUGRID_CONFORM
  #if HAVE_GRIDTYPE
    #error "Ambiguous definition of GRIDTYPE."
  #endif
  #if not HAVE_ALUGRID
    #error "ALUGRID_CONFORM defined but no ALUGRID version found!"
  #endif
  #if (GRIDDIM != 2) || (WORLDDIM != GRIDDIM)
    #error "ALUGRID_CONFORM is only available for GRIDDIM=2 and WORLDDIM=GRIDDIM."
  #endif
  #include <dune/grid/alugrid.hh>
namespace Dune
{
  namespace GridSelector
  {
    typedef Dune :: ALUConformGrid< dimgrid, dimworld > GridType;
  }
}
  #define HAVE_GRIDTYPE 1
#endif


// Check OneDGrid
#if defined ONEDGRID
  #if HAVE_GRIDTYPE
    #error "Ambiguous definition of GRIDTYPE."
  #endif
  #if (GRIDDIM != 1) || (WORLDDIM != GRIDDIM)
    #error "ONEDGRID is only available for GRIDDIM=1 and WORLDDIM=GRIDDIM."
  #endif

  #include <dune/grid/onedgrid.hh>
namespace Dune
{
  namespace GridSelector
  {
    typedef Dune :: OneDGrid GridType;
  }
}
  #define HAVE_GRIDTYPE 1
#endif


// Check SGrid
#if defined SGRID
  #if HAVE_GRIDTYPE
    #error "Ambiguous definition of GRIDTYPE."
  #endif

  #include <dune/grid/sgrid.hh>
namespace Dune
{
  namespace GridSelector
  {
    typedef Dune :: SGrid< dimgrid, dimworld > GridType;
  }
}
  #define HAVE_GRIDTYPE 1
#endif


// Check UGGrid
#if defined UGGRID
  #if HAVE_GRIDTYPE
    #error "Ambiguous definition of GRIDTYPE."
  #endif
  #if not HAVE_UG
    #error "UGGRID defined but no UG version found!"
  #endif
  #if (GRIDDIM < 2) || (GRIDDIM > 3)
    #error "UGGRID is only available for GRIDDIM=2 and GRIDDIM=3."
  #endif
  #if (GRIDDIM != WORLDDIM)
    #error "UGGRID only supports GRIDDIM=WORLDDIM."
  #endif

  #include <dune/grid/uggrid.hh>
namespace Dune
{
  namespace GridSelector
  {
    typedef Dune :: UGGrid< dimgrid > GridType;
  }
}
  #define HAVE_GRIDTYPE 1
#endif


// Check YASPGrid
#if defined YASPGRID
  #if HAVE_GRIDTYPE
    #error "Ambiguous definition of GRIDTYPE."
  #endif
  #if (GRIDDIM != WORLDDIM)
    #error "YASPGRID only supports GRIDDIM=WORLDDIM."
  #endif

  #include <dune/grid/yaspgrid.hh>
namespace Dune
{
  namespace GridSelector
  {
    typedef Dune :: YaspGrid< dimgrid > GridType;
  }
}
  #define HAVE_GRIDTYPE 1
#endif

// NOGRID is used to specify that no default was set during configure
// If NOGRID and HAVE_GRIDTYPE are both not set then no grid was selected
// and an error is produced
#if defined NOGRID
  #if ! HAVE_GRIDTYPE
    #error "No grid type selected, use GRIDTYPE=... or use the with-gridtype switch during configure to set a default."
  #endif
#endif

typedef Dune::GridSelector::GridType GridType DUNE_DEPRECATED;

#endif
