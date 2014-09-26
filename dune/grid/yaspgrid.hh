// -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi: set et ts=4 sw=2 sts=2:
#ifndef DUNE_GRID_YASPGRID_HH
#define DUNE_GRID_YASPGRID_HH

#include <iostream>
#include <vector>
#include <algorithm>
#include <stack>

// either include stdint.h or provide fallback for uint8_t
#if HAVE_STDINT_H
#include <stdint.h>
#else
typedef unsigned char uint8_t;
#endif

#include <dune/grid/common/backuprestore.hh>
#include <dune/grid/common/grid.hh>     // the grid base classes
#include <dune/grid/common/capabilities.hh> // the capabilities
#include <dune/common/power.hh>
#include <dune/common/shared_ptr.hh>
#include <dune/common/bigunsignedint.hh>
#include <dune/common/typetraits.hh>
#include <dune/common/reservedvector.hh>
#include <dune/common/parallel/collectivecommunication.hh>
#include <dune/common/parallel/mpihelper.hh>
#include <dune/geometry/genericgeometry/topologytypes.hh>
#include <dune/geometry/axisalignedcubegeometry.hh>
#include <dune/grid/common/indexidset.hh>
#include <dune/grid/common/datahandleif.hh>


#if HAVE_MPI
#include <dune/common/parallel/mpicollectivecommunication.hh>
#endif

/*! \file yaspgrid.hh
   YaspGrid stands for yet another structured parallel grid.
   It will implement the dune grid interface for structured grids with codim 0
   and dim, with arbitrary overlap, parallel features with two overlap
   models, periodic boundaries and fast a implementation allowing on-the-fly computations.
 */

namespace Dune {

  /* some sizes for building global ids
   */
  const int yaspgrid_dim_bits = 24; // bits for encoding each dimension
  const int yaspgrid_level_bits = 5; // bits for encoding level number


  //************************************************************************
  // forward declaration of templates

  template<int dim, class CoordCont>                             class YaspGrid;
  template<int mydim, int cdim, class GridImp>  class YaspGeometry;
  template<int codim, int dim, class GridImp>   class YaspEntity;
  template<int codim, class GridImp>            class YaspEntityPointer;
  template<int codim, class GridImp>            class YaspEntitySeed;
  template<int codim, PartitionIteratorType pitype, class GridImp> class YaspLevelIterator;
  template<class GridImp>            class YaspIntersectionIterator;
  template<class GridImp>            class YaspIntersection;
  template<class GridImp>            class YaspHierarchicIterator;
  template<class GridImp, bool isLeafIndexSet>                     class YaspIndexSet;
  template<class GridImp>            class YaspGlobalIdSet;

} // namespace Dune

#include <dune/grid/yaspgrid/coordinates.hh>
#include <dune/grid/yaspgrid/torus.hh>
#include <dune/grid/yaspgrid/ygrid.hh>
#include <dune/grid/yaspgrid/yaspgridgeometry.hh>
#include <dune/grid/yaspgrid/yaspgridentity.hh>
#include <dune/grid/yaspgrid/yaspgridintersection.hh>
#include <dune/grid/yaspgrid/yaspgridintersectioniterator.hh>
#include <dune/grid/yaspgrid/yaspgridhierarchiciterator.hh>
#include <dune/grid/yaspgrid/yaspgridentityseed.hh>
#include <dune/grid/yaspgrid/yaspgridentitypointer.hh>
#include <dune/grid/yaspgrid/yaspgridleveliterator.hh>
#include <dune/grid/yaspgrid/yaspgridindexsets.hh>
#include <dune/grid/yaspgrid/yaspgrididset.hh>

namespace Dune {

  template<int dim, class CoordCont>
  struct YaspGridFamily
  {
#if HAVE_MPI
    typedef CollectiveCommunication<MPI_Comm> CCType;
#else
    typedef CollectiveCommunication<Dune::YaspGrid<dim, CoordCont> > CCType;
#endif

    typedef GridTraits<dim,                                     // dimension of the grid
        dim,                                                    // dimension of the world space
        Dune::YaspGrid<dim, CoordCont>,
        YaspGeometry,YaspEntity,
        YaspEntityPointer,
        YaspLevelIterator,                                      // type used for the level iterator
        YaspIntersection,              // leaf  intersection
        YaspIntersection,              // level intersection
        YaspIntersectionIterator,              // leaf  intersection iter
        YaspIntersectionIterator,              // level intersection iter
        YaspHierarchicIterator,
        YaspLevelIterator,                                      // type used for the leaf(!) iterator
        YaspIndexSet< const YaspGrid< dim, CoordCont >, false >,                  // level index set
        YaspIndexSet< const YaspGrid< dim, CoordCont >, true >,                  // leaf index set
        YaspGlobalIdSet<const YaspGrid<dim, CoordCont> >,
        bigunsignedint<dim*yaspgrid_dim_bits+yaspgrid_level_bits+dim>,
        YaspGlobalIdSet<const YaspGrid<dim, CoordCont> >,
        bigunsignedint<dim*yaspgrid_dim_bits+yaspgrid_level_bits+dim>,
        CCType,
        DefaultLevelGridViewTraits, DefaultLeafGridViewTraits,
        YaspEntitySeed>
    Traits;
  };

#ifndef DOXYGEN
  template<int dim, int codim>
  struct YaspCommunicateMeta {
    template<class G, class DataHandle>
    static void comm (const G& g, DataHandle& data, InterfaceType iftype, CommunicationDirection dir, int level)
    {
      if (data.contains(dim,codim))
      {
        g.template communicateCodim<DataHandle,codim>(data,iftype,dir,level);
      }
      YaspCommunicateMeta<dim,codim-1>::comm(g,data,iftype,dir,level);
    }
  };

  template<int dim>
  struct YaspCommunicateMeta<dim,0> {
    template<class G, class DataHandle>
    static void comm (const G& g, DataHandle& data, InterfaceType iftype, CommunicationDirection dir, int level)
    {
      if (data.contains(dim,0))
        g.template communicateCodim<DataHandle,0>(data,iftype,dir,level);
    }
  };
#endif

  //************************************************************************
  /*!
     \brief [<em> provides \ref Dune::Grid </em>]
     \brief Provides a distributed structured cube mesh.
     \ingroup GridImplementations

     YaspGrid stands for yet another structured parallel grid.
     It implements the dune grid interface for structured grids with codim 0
     and dim, with arbitrary overlap (including zero),
     periodic boundaries and fast implementation allowing on-the-fly computations.

     \tparam dim The dimension of the grid and its surrounding world

     \par History:
     \li started on July 31, 2004 by PB based on abstractions developed in summer 2003
   */
  template<int dim, class CoordCont = EquidistantCoordinates<double, dim> >
  class YaspGrid
    : public GridDefaultImplementation<dim,dim,typename CoordCont::ctype,YaspGridFamily<dim, CoordCont> >
  {
  public:
    //! Type used for coordinates
    typedef typename CoordCont::ctype ctype;

#ifndef DOXYGEN
    typedef typename Dune::YGrid<CoordCont> YGrid;
    typedef typename Dune::YGridList<CoordCont>::Intersection Intersection;

    /** \brief A single grid level within a YaspGrid
     */
    struct YGridLevel {

      /** \brief Level number of this level grid */
      int level() const
      {
        return level_;
      }

      CoordCont coords;

      Dune::array<YGrid, dim+1> overlapfront;
      Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power> overlapfront_data;
      Dune::array<YGrid, dim+1> overlap;
      Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power> overlap_data;
      Dune::array<YGrid, dim+1> interiorborder;
      Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power> interiorborder_data;
      Dune::array<YGrid, dim+1> interior;
      Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power> interior_data;

      Dune::array<YGridList<CoordCont>,dim+1> send_overlapfront_overlapfront;
      Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>  send_overlapfront_overlapfront_data;
      Dune::array<YGridList<CoordCont>,dim+1> recv_overlapfront_overlapfront;
      Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>  recv_overlapfront_overlapfront_data;

      Dune::array<YGridList<CoordCont>,dim+1> send_overlap_overlapfront;
      Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>  send_overlap_overlapfront_data;
      Dune::array<YGridList<CoordCont>,dim+1> recv_overlapfront_overlap;
      Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>  recv_overlapfront_overlap_data;

      Dune::array<YGridList<CoordCont>,dim+1> send_interiorborder_interiorborder;
      Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>  send_interiorborder_interiorborder_data;
      Dune::array<YGridList<CoordCont>,dim+1> recv_interiorborder_interiorborder;
      Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>  recv_interiorborder_interiorborder_data;

      Dune::array<YGridList<CoordCont>,dim+1> send_interiorborder_overlapfront;
      Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>  send_interiorborder_overlapfront_data;
      Dune::array<YGridList<CoordCont>,dim+1> recv_overlapfront_interiorborder;
      Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>  recv_overlapfront_interiorborder_data;

      // general
      YaspGrid<dim,CoordCont>* mg;  // each grid level knows its multigrid
      int overlapSize;           // in mesh cells on this level

      /** \brief The level number within the YaspGrid level hierarchy */
      int level_;
    };

    //! define types used for arguments
    typedef Dune::array<int, dim> iTupel;
    typedef FieldVector<ctype, dim> fTupel;

    // communication tag used by multigrid
    enum { tag = 17 };
#endif

    //! return reference to torus
    const Torus<dim>& torus () const
    {
      return _torus;
    }

    //! return number of cells on finest level in given direction on all processors
    int globalSize(int i) const
    {
      return levelSize(maxLevel(),i);
    }

    //! return number of cells on finest level on all processors
    iTupel globalSize() const
    {
      return levelSize(maxLevel());
    }

    //! return size of the grid (in cells) on level l in direction i
    int levelSize(int l, int i) const
    {
      return _coarseSize[i] * (1 << l);
    }

    //! return size vector of the grid (in cells) on level l
    iTupel levelSize(int l) const
    {
      iTupel s;
      for (int i=0; i<dim; ++i)
        s[i] = levelSize(l,i);
      return s;
    }

    //! return whether the grid is periodic in direction i
    bool isPeriodic(int i) const
    {
      return _periodic[i];
    }

    bool getRefineOption() const
    {
      return keep_ovlp;
    }

    //! Iterator over the grid levels
    typedef typename ReservedVector<YGridLevel,32>::const_iterator YGridLevelIterator;

    //! return iterator pointing to coarsest level
    YGridLevelIterator begin () const
    {
      return YGridLevelIterator(_levels,0);
    }

    //! return iterator pointing to given level
    YGridLevelIterator begin (int i) const
    {
      if (i<0 || i>maxLevel())
        DUNE_THROW(GridError, "level not existing");
      return YGridLevelIterator(_levels,i);
    }

    //! return iterator pointing to one past the finest level
    YGridLevelIterator end () const
    {
      return YGridLevelIterator(_levels,maxLevel()+1);
    }

    // static method to create the default load balance strategy
    static const YLoadBalance<dim>* defaultLoadbalancer()
    {
      static YLoadBalance<dim> lb;
      return & lb;
    }

  protected:
    /** \brief Make a new YGridLevel structure
     *
     * \param coords      the coordinate container
     * \param periodic    indicate periodicity for each direction
     * \param o_interior  origin of interior (non-overlapping) cell decomposition
     * \param overlap     to be used on this grid level
     */
    void makelevel (const CoordCont& coords, std::bitset<dim> periodic, iTupel o_interior, int overlap)
    {
      YGridLevel& g = _levels.back();
      g.overlapSize = overlap;
      g.mg = this;
      g.level_ = maxLevel();
      g.coords = coords;

      // set the inserting positions in the corresponding arrays of YGridLevelStructure
      typename Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power>::iterator overlapfront_it = g.overlapfront_data.begin();
      typename Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power>::iterator overlap_it = g.overlap_data.begin();
      typename Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power>::iterator interiorborder_it = g.interiorborder_data.begin();
      typename Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power>::iterator interior_it = g.interior_data.begin();

      typename Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>::iterator
        send_overlapfront_overlapfront_it = g.send_overlapfront_overlapfront_data.begin();
      typename Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>::iterator
        recv_overlapfront_overlapfront_it = g.recv_overlapfront_overlapfront_data.begin();

      typename Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>::iterator
        send_overlap_overlapfront_it = g.send_overlap_overlapfront_data.begin();
      typename Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>::iterator
        recv_overlapfront_overlap_it = g.recv_overlapfront_overlap_data.begin();

      typename Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>::iterator
        send_interiorborder_interiorborder_it = g.send_interiorborder_interiorborder_data.begin();
      typename Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>::iterator
        recv_interiorborder_interiorborder_it = g.recv_interiorborder_interiorborder_data.begin();

      typename Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>::iterator
        send_interiorborder_overlapfront_it = g.send_interiorborder_overlapfront_data.begin();
      typename Dune::array<std::deque<Intersection>, StaticPower<2,dim>::power>::iterator
        recv_overlapfront_interiorborder_it = g.recv_overlapfront_interiorborder_data.begin();

      // have a null array for constructor calls around
      Dune::array<int,dim> n;
      std::fill(n.begin(), n.end(), 0);

      // determine origin of the grid with overlap and store whether an overlap area exists in direction i.
      std::bitset<dim> ovlp_low(0ULL);
      std::bitset<dim> ovlp_up(0ULL);

      iTupel o_overlap;
      iTupel s_overlap;

      // determine at where we have overlap and how big the size of the overlap partition is
      for (int i=0; i<dim; i++)
      {
        // the coordinate container has been contructed to hold the entire grid on
        // this processor, including overlap. this is the element size.
        s_overlap[i] = g.coords.size(i);

        //in the periodic case there is always overlap
        if (periodic[i])
        {
          o_overlap[i] = o_interior[i]-overlap;
          ovlp_low[i] = true;
          ovlp_up[i] = true;
        }
        else
        {
          //check lower boundary
          if (o_interior[i] - overlap < 0)
            o_overlap[i] = 0;
          else
          {
            o_overlap[i] = o_interior[i] - overlap;
            ovlp_low[i] = true;
          }

          //check upper boundary
          if (o_overlap[i] + g.coords.size(i) < globalSize(i))
            ovlp_up[i] = true;
        }
      }

      for (int codim = 0; codim < dim + 1; codim++)
      {
        // set the begin iterator for the corresponding ygrids
        g.overlapfront[codim].setBegin(overlapfront_it);
        g.overlap[codim].setBegin(overlap_it);
        g.interiorborder[codim].setBegin(interiorborder_it);
        g.interior[codim].setBegin(interior_it);
        g.send_overlapfront_overlapfront[codim].setBegin(send_overlapfront_overlapfront_it);
        g.recv_overlapfront_overlapfront[codim].setBegin(recv_overlapfront_overlapfront_it);
        g.send_overlap_overlapfront[codim].setBegin(send_overlap_overlapfront_it);
        g.recv_overlapfront_overlap[codim].setBegin(recv_overlapfront_overlap_it);
        g.send_interiorborder_interiorborder[codim].setBegin(send_interiorborder_interiorborder_it);
        g.recv_interiorborder_interiorborder[codim].setBegin(recv_interiorborder_interiorborder_it);
        g.send_interiorborder_overlapfront[codim].setBegin(send_interiorborder_overlapfront_it);
        g.recv_overlapfront_interiorborder[codim].setBegin(recv_overlapfront_interiorborder_it);

        // find all combinations of unit vectors that span entities of the given codimension
        for (unsigned int index = 0; index < (1<<dim); index++)
        {
          // check whether the given shift is of our codimension
          std::bitset<dim> r(index);
          if (r.count() != dim-codim)
            continue;

          // get an origin and a size array for subsequent modification
          Dune::array<int,dim> origin(o_overlap);
          Dune::array<int,dim> size(s_overlap);

          // build overlapfront
          // we have to extend the element size by one in all directions without shift.
          for (int i=0; i<dim; i++)
            if (!r[i])
              size[i]++;
          *overlapfront_it = YGridComponent<CoordCont>(origin, r, &g.coords, size, n, size);

          // build overlap
          for (int i=0; i<dim; i++)
          {
            if (!r[i])
            {
              if (ovlp_low[i])
              {
                origin[i]++;
                size[i]--;
              }
              if (ovlp_up[i])
                size[i]--;
            }
          }
          *overlap_it = YGridComponent<CoordCont>(origin,size,*overlapfront_it);

          // build interiorborder
          for (int i=0; i<dim; i++)
          {
            if (ovlp_low[i])
            {
              origin[i] += overlap;
              size[i] -= overlap;
              if (!r[i])
              {
                origin[i]--;
                size[i]++;
              }
            }
            if (ovlp_up[i])
            {
              size[i] -= overlap;
              if (!r[i])
                size[i]++;
            }
          }
          *interiorborder_it = YGridComponent<CoordCont>(origin,size,*overlapfront_it);

          // build interior
          for (int i=0; i<dim; i++)
          {
            if (!r[i])
            {
              if (ovlp_low[i])
              {
                origin[i]++;
                size[i]--;
              }
              if (ovlp_up[i])
                size[i]--;
            }
          }
          *interior_it = YGridComponent<CoordCont>(origin, size, *overlapfront_it);

          intersections(*overlapfront_it,*overlapfront_it,*send_overlapfront_overlapfront_it, *recv_overlapfront_overlapfront_it);
          intersections(*overlap_it,*overlapfront_it,*send_overlap_overlapfront_it, *recv_overlapfront_overlap_it);
          intersections(*interiorborder_it,*interiorborder_it,*send_interiorborder_interiorborder_it,*recv_interiorborder_interiorborder_it);
          intersections(*interiorborder_it,*overlapfront_it,*send_interiorborder_overlapfront_it,*recv_overlapfront_interiorborder_it);

          // advance all iterators pointing to the next insertion point
          ++overlapfront_it;
          ++overlap_it;
          ++interiorborder_it;
          ++interior_it;
          ++send_overlapfront_overlapfront_it;
          ++recv_overlapfront_overlapfront_it;
          ++send_overlap_overlapfront_it;
          ++recv_overlapfront_overlap_it;
          ++send_interiorborder_interiorborder_it;
          ++recv_interiorborder_interiorborder_it;
          ++send_interiorborder_overlapfront_it;
          ++recv_overlapfront_interiorborder_it;
        }

        // set end iterators in the corresonding ygrids
        g.overlapfront[codim].finalize(overlapfront_it);
        g.overlap[codim].finalize(overlap_it);
        g.interiorborder[codim].finalize(interiorborder_it);
        g.interior[codim].finalize(interior_it);
        g.send_overlapfront_overlapfront[codim].finalize(send_overlapfront_overlapfront_it);
        g.recv_overlapfront_overlapfront[codim].finalize(recv_overlapfront_overlapfront_it);
        g.send_overlap_overlapfront[codim].finalize(send_overlap_overlapfront_it);
        g.recv_overlapfront_overlap[codim].finalize(recv_overlapfront_overlap_it);
        g.send_interiorborder_interiorborder[codim].finalize(send_interiorborder_interiorborder_it);
        g.recv_interiorborder_interiorborder[codim].finalize(recv_interiorborder_interiorborder_it);
        g.send_interiorborder_overlapfront[codim].finalize(send_interiorborder_overlapfront_it);
        g.recv_overlapfront_interiorborder[codim].finalize(recv_overlapfront_interiorborder_it);
      }
    }

#ifndef DOXYGEN
    /** \brief special data structure to communicate ygrids
     * Historically, this was needed because Ygrids had virtual functions and
     * a communicated virtual function table pointer introduced a bug. After the
     * change to tensorproductgrid, the dynamic polymorphism was removed, still this
     * is kept because it allows to communicate ygrids, that only have index, but no
     * coordinate information. This is sufficient, because all communicated YGrids are
     * intersected with a local grid, which has coordinate information.
     */
    struct mpifriendly_ygrid {
      mpifriendly_ygrid ()
      {
        std::fill(origin.begin(), origin.end(), 0);
        std::fill(size.begin(), size.end(), 0);
      }
      mpifriendly_ygrid (const YGridComponent<CoordCont>& grid)
        : origin(grid.origin()), size(grid.size())
      {}
      iTupel origin;
      iTupel size;
    };
#endif

    /** \brief Construct list of intersections with neighboring processors
     *
     * \param recvgrid the grid stored in this processor
     * \param sendgrid the subgrid to be sent to neighboring processors
     * \param sendlist the deque to fill with send intersections
     * \param recvlist the deque to fill with recv intersections
     * \returns two lists: Intersections to be sent and Intersections to be received
     */
    void intersections(const YGridComponent<CoordCont>& sendgrid, const YGridComponent<CoordCont>& recvgrid,
                        std::deque<Intersection>& sendlist, std::deque<Intersection>& recvlist)
    {
      iTupel size = globalSize();

      // the exchange buffers
      std::vector<YGridComponent<CoordCont> > send_recvgrid(_torus.neighbors());
      std::vector<YGridComponent<CoordCont> > recv_recvgrid(_torus.neighbors());
      std::vector<YGridComponent<CoordCont> > send_sendgrid(_torus.neighbors());
      std::vector<YGridComponent<CoordCont> > recv_sendgrid(_torus.neighbors());

      // new exchange buffers to send simple struct without virtual functions
      std::vector<mpifriendly_ygrid> mpifriendly_send_recvgrid(_torus.neighbors());
      std::vector<mpifriendly_ygrid> mpifriendly_recv_recvgrid(_torus.neighbors());
      std::vector<mpifriendly_ygrid> mpifriendly_send_sendgrid(_torus.neighbors());
      std::vector<mpifriendly_ygrid> mpifriendly_recv_sendgrid(_torus.neighbors());

      // fill send buffers; iterate over all neighboring processes
      // non-periodic case is handled automatically because intersection will be zero
      for (typename Torus<dim>::ProcListIterator i=_torus.sendbegin(); i!=_torus.sendend(); ++i)
      {
        // determine if we communicate with this neighbor (and what)
        bool skip = false;
        iTupel coord = _torus.coord();   // my coordinates
        iTupel delta = i.delta();        // delta to neighbor
        iTupel nb = coord;               // the neighbor
        for (int k=0; k<dim; k++) nb[k] += delta[k];
        iTupel v;                    // grid movement
        std::fill(v.begin(), v.end(), 0);

        for (int k=0; k<dim; k++)
        {
          if (nb[k]<0)
          {
            if (_periodic[k])
              v[k] += size[k];
            else
              skip = true;
          }
          if (nb[k]>=_torus.dims(k))
          {
            if (_periodic[k])
              v[k] -= size[k];
            else
              skip = true;
          }
          // neither might be true, then v=0
        }

        // store moved grids in send buffers
        if (!skip)
        {
          send_sendgrid[i.index()] = sendgrid.move(v);
          send_recvgrid[i.index()] = recvgrid.move(v);
        }
        else
        {
          send_sendgrid[i.index()] = YGridComponent<CoordCont>();
          send_recvgrid[i.index()] = YGridComponent<CoordCont>();
        }
      }

      // issue send requests for sendgrid being sent to all neighbors
      for (typename Torus<dim>::ProcListIterator i=_torus.sendbegin(); i!=_torus.sendend(); ++i)
      {
        mpifriendly_send_sendgrid[i.index()] = mpifriendly_ygrid(send_sendgrid[i.index()]);
        _torus.send(i.rank(), &mpifriendly_send_sendgrid[i.index()], sizeof(mpifriendly_ygrid));
      }

      // issue recv requests for sendgrids of neighbors
      for (typename Torus<dim>::ProcListIterator i=_torus.recvbegin(); i!=_torus.recvend(); ++i)
        _torus.recv(i.rank(), &mpifriendly_recv_sendgrid[i.index()], sizeof(mpifriendly_ygrid));

      // exchange the sendgrids
      _torus.exchange();

      // issue send requests for recvgrid being sent to all neighbors
      for (typename Torus<dim>::ProcListIterator i=_torus.sendbegin(); i!=_torus.sendend(); ++i)
      {
        mpifriendly_send_recvgrid[i.index()] = mpifriendly_ygrid(send_recvgrid[i.index()]);
        _torus.send(i.rank(), &mpifriendly_send_recvgrid[i.index()], sizeof(mpifriendly_ygrid));
      }

      // issue recv requests for recvgrid of neighbors
      for (typename Torus<dim>::ProcListIterator i=_torus.recvbegin(); i!=_torus.recvend(); ++i)
        _torus.recv(i.rank(), &mpifriendly_recv_recvgrid[i.index()], sizeof(mpifriendly_ygrid));

      // exchange the recvgrid
      _torus.exchange();

      // process receive buffers and compute intersections
      for (typename Torus<dim>::ProcListIterator i=_torus.recvbegin(); i!=_torus.recvend(); ++i)
      {
        // what must be sent to this neighbor
        Intersection send_intersection;
        mpifriendly_ygrid yg = mpifriendly_recv_recvgrid[i.index()];
        recv_recvgrid[i.index()] = YGridComponent<CoordCont>(yg.origin,yg.size);
        send_intersection.grid = sendgrid.intersection(recv_recvgrid[i.index()]);
        send_intersection.rank = i.rank();
        send_intersection.distance = i.distance();
        if (!send_intersection.grid.empty()) sendlist.push_front(send_intersection);

        Intersection recv_intersection;
        yg = mpifriendly_recv_sendgrid[i.index()];
        recv_sendgrid[i.index()] = YGridComponent<CoordCont>(yg.origin,yg.size);
        recv_intersection.grid = recvgrid.intersection(recv_sendgrid[i.index()]);
        recv_intersection.rank = i.rank();
        recv_intersection.distance = i.distance();
        if(!recv_intersection.grid.empty()) recvlist.push_back(recv_intersection);
      }
    }

  protected:

    typedef const YaspGrid<dim,CoordCont> GridImp;

    void init()
    {
      Yasp::BinomialTable<dim>::init();
      Yasp::EntityShiftTable<Yasp::calculate_entity_shift<dim>,dim>::init();
      Yasp::EntityShiftTable<Yasp::calculate_entity_move<dim>,dim>::init();
      indexsets.push_back( make_shared< YaspIndexSet<const YaspGrid<dim, CoordCont>, false > >(*this,0) );
      boundarysegmentssize();
    }

    void boundarysegmentssize()
    {
      // sizes of local macro grid
      Dune::array<int, dim> sides;
      {
        for (int i=0; i<dim; i++)
        {
          sides[i] =
            ((begin()->overlap[0].dataBegin()->origin(i) == 0)+
             (begin()->overlap[0].dataBegin()->origin(i) + begin()->overlap[0].dataBegin()->size(i)
                    == levelSize(0,i)));
        }
      }
      nBSegments = 0;
      for (int k=0; k<dim; k++)
      {
        int offset = 1;
        for (int l=0; l<dim; l++)
        {
          if (l==k) continue;
          offset *= begin()->overlap[0].dataBegin()->size(l);
        }
        nBSegments += sides[k]*offset;
      }
    }

  public:

    // define the persistent index type
    typedef bigunsignedint<dim*yaspgrid_dim_bits+yaspgrid_level_bits+dim> PersistentIndexType;

    //! the GridFamily of this grid
    typedef YaspGridFamily<dim, CoordCont> GridFamily;
    // the Traits
    typedef typename YaspGridFamily<dim, CoordCont>::Traits Traits;

    // need for friend declarations in entity
    typedef YaspIndexSet<YaspGrid<dim, CoordCont>, false > LevelIndexSetType;
    typedef YaspIndexSet<YaspGrid<dim, CoordCont>, true > LeafIndexSetType;
    typedef YaspGlobalIdSet<YaspGrid<dim, CoordCont> > GlobalIdSetType;

    //! correctly initialize a tensorproduct Yaspgrid from information given in the constructor
    void TensorProductSetup(Dune::array<std::vector<ctype>,dim> coords, std::bitset<dim> periodic, int overlap, const YLoadBalance<dim>* lb = defaultLoadbalancer())
    {
      _periodic = periodic;
      _levels.resize(1);
      _overlap = overlap;

      //determine sizes of vector to correctly construct torus structure and store for later size requests
      for (int i=0; i<dim; i++)
        _coarseSize[i] = coords[i].size() - 1;

      iTupel o;
      std::fill(o.begin(), o.end(), 0);
      iTupel o_interior(o);
      iTupel s_interior(_coarseSize);

#if HAVE_MPI
      double imbal = _torus.partition(_torus.rank(),o,_coarseSize,o_interior,s_interior);
      imbal = _torus.global_max(imbal);
#endif

      Dune::array<std::vector<ctype>,dim> newcoords;
      Dune::array<int, dim> offset(o_interior);

      // find the relevant part of the coords vector for this processor and copy it to newcoords
      for (int i=0; i<dim; ++i)
      {
        //define iterators on coords that specify the coordinate range to be used
        typename std::vector<ctype>::iterator begin = coords[i].begin() + o_interior[i];
        typename std::vector<ctype>::iterator end = begin + s_interior[i] + 1;

        // check whether we are not at the physical boundary. In that case overlap is a simple
        // extension of the coordinate range to be used
        if (o_interior[i] - overlap > 0)
        {
          begin = begin - overlap;
          offset[i] -= overlap;
        }
        if (o_interior[i] + s_interior[i] + overlap < _coarseSize[i])
          end = end + overlap;

        //copy the selected part in the new coord vector
        newcoords[i].resize(end-begin);
        std::copy(begin, end, newcoords[i].begin());

        // check whether we are at the physical boundary and a have a periodic grid.
        // In this case the coordinate vector has to be tweaked manually.
        if ((periodic[i]) && (o_interior[i] + s_interior[i] + overlap >= _coarseSize[i]))
        {
          // we need to add the first <overlap> cells to the end of newcoords
          typename std::vector<ctype>::iterator it = coords[i].begin();
          for (int j=0; j<overlap; ++j)
            newcoords[i].push_back(newcoords[i].back() - *it + *(++it));
        }

        if ((periodic[i]) && (o_interior[i] - overlap <= 0))
        {
          offset[i] -= overlap;

          // we need to add the last <overlap> cells to the begin of newcoords
          typename std::vector<ctype>::iterator it = coords[i].end() - 1;
          for (int j=0; j<overlap; ++j)
            newcoords[i].insert(newcoords[i].begin(), newcoords[i].front() - *it + *(--it));
        }
      }

      // check whether YaspGrid has been given the correct template parameter
      static_assert(is_same<CoordCont,TensorProductCoordinates<ctype,dim> >::value,
        "YaspGrid coordinate container template parameter and given constructor values do not match!");

      TensorProductCoordinates<ctype,dim> cc(newcoords, offset);

      // add level
      makelevel(cc,periodic,o_interior,overlap);
    }

    //! correctly initialize an equidistant grid from the information given in the constructor
    void EquidistantSetup(fTupel L, iTupel s, std::bitset<dim> periodic, int overlap, const YLoadBalance<dim>* lb = defaultLoadbalancer())
    {
      _periodic = periodic;
      _levels.resize(1);
      _overlap = overlap;
      _coarseSize = s;

      iTupel o;
      std::fill(o.begin(), o.end(), 0);
      iTupel o_interior(o);
      iTupel s_interior(s);

#if HAVE_MPI
      double imbal = _torus.partition(_torus.rank(),o,s,o_interior,s_interior);
      imbal = _torus.global_max(imbal);
#endif

      fTupel h(L);
      for (int i=0; i<dim; i++)
        h[i] /= s[i];

      iTupel s_overlap(s_interior);
      for (int i=0; i<dim; i++)
      {
        if ((o_interior[i] - overlap > 0) || (periodic[i]))
          s_overlap[i] += overlap;
        if ((o_interior[i] + s_interior[i] + overlap <= _coarseSize[i]) || (periodic[i]))
          s_overlap[i] += overlap;
      }

      // check whether YaspGrid has been given the correct template parameter
      static_assert(is_same<CoordCont,EquidistantCoordinates<ctype,dim> >::value,
        "YaspGrid coordinate container template parameter and given constructor values do not match!");

      EquidistantCoordinates<ctype,dim> cc(h,s_overlap);

      // add level
      makelevel(cc,periodic,o_interior,overlap);
    }

    /*! Constructor
       @param comm MPI communicator where this mesh is distributed to
       @param L extension of the domain
       @param s number of cells on coarse mesh in each direction
       @param periodic tells if direction is periodic or not
       @param overlap size of overlap on coarsest grid (same in all directions)
       @param lb pointer to an overloaded YLoadBalance instance
     */
    YaspGrid (Dune::MPIHelper::MPICommunicator comm,
              Dune::FieldVector<ctype, dim> L,
              Dune::array<int, dim> s,
              std::bitset<dim> periodic,
              int overlap,
              const YLoadBalance<dim>* lb = defaultLoadbalancer())
#if HAVE_MPI
      : ccobj(comm),
        _torus(comm,tag,s,lb),
#else
      : _torus(tag,s,lb),
#endif
        leafIndexSet_(*this),
        keep_ovlp(true), adaptRefCount(0), adaptActive(false)
    {
      EquidistantSetup(L,s,periodic,overlap,lb);
      init();
    }


    /*! Constructor for a sequential YaspGrid

       Sequential here means that the whole grid is living on one process even if your program is running
       in parallel.
       @see YaspGrid(Dune::MPIHelper::MPICommunicator, Dune::FieldVector<ctype, dim>, Dune::FieldVector<int, dim>,  Dune::FieldVector<bool, dim>, int)
       for constructing one parallel grid decomposed between the processors.
       @param L extension of the domain
       @param s number of cells on coarse mesh in each direction
       @param periodic tells if direction is periodic or not
       @param overlap size of overlap on coarsest grid (same in all directions)
       @param lb pointer to an overloaded YLoadBalance instance
     */
    YaspGrid (Dune::FieldVector<ctype, dim> L,
              Dune::array<int, dim> s,
              std::bitset<dim> periodic,
              int overlap,
              const YLoadBalance<dim>* lb = defaultLoadbalancer())
#if HAVE_MPI
      : ccobj(MPI_COMM_SELF),
        _torus(MPI_COMM_SELF,tag,s,lb),
#else
      : _torus(tag,s,lb),
#endif
        leafIndexSet_(*this),
        keep_ovlp(true), adaptRefCount(0), adaptActive(false)
    {
      EquidistantSetup(L,s,periodic,overlap,lb);
      init();
    }

    /*! Constructor for a sequential YaspGrid without periodicity

       Sequential here means that the whole grid is living on one process even if your program is running
       in parallel.
       @see YaspGrid(Dune::MPIHelper::MPICommunicator, Dune::FieldVector<ctype, dim>, Dune::FieldVector<int, dim>,  Dune::FieldVector<bool, dim>, int)
       for constructing one parallel grid decomposed between the processors.
       @param L extension of the domain (lower left is always (0,...,0)
       @param elements number of cells on coarse mesh in each direction
     */
    YaspGrid (Dune::FieldVector<ctype, dim> L,
              Dune::array<int, dim> elements)
#if HAVE_MPI
      : ccobj(MPI_COMM_SELF),
        _torus(MPI_COMM_SELF,tag,elements,defaultLoadbalancer()),
#else
      : _torus(tag,elements,defaultLoadbalancer()),
#endif
        leafIndexSet_(*this),
        keep_ovlp(true), adaptRefCount(0), adaptActive(false)
    {
      EquidistantSetup(L,elements,std::bitset<dim>(0ULL),0,defaultLoadbalancer());
      init();
    }

    /** @brief Constructor for a sequential tensorproduct YaspGrid without periodicity
     *  Sequential here means that the whole grid is living on one process even if your program is running
     *  in parallel.
     *  @see YaspGrid(Dune::MPIHelper::MPICommunicator, Dune::array<std::vector<ctype>,dim>, std::bitset<dim>, int)
     *  for constructing one parallel grid decomposed between the processors.
     *  @param coords coordinate vectors to be used for coarse grid
     */
    YaspGrid (Dune::array<std::vector<ctype>, dim> coords)
#if HAVE_MPI
      : ccobj(MPI_COMM_SELF),
        _torus(MPI_COMM_SELF,tag,Dune::Yasp::sizeArray<dim>(coords),defaultLoadbalancer()),
#else
      : _torus(tag,Dune::Yasp::sizeArray<dim>(coords),defaultLoadbalancer()),
#endif
        leafIndexSet_(*this),
        _periodic(std::bitset<dim>(0)),
        _overlap(0),
        keep_ovlp(true),
        adaptRefCount(0), adaptActive(false)
    {
      if (!Dune::Yasp::checkIfMonotonous(coords))
        DUNE_THROW(Dune::GridError,"Setup of a tensorproduct grid requires monotonous sequences of coordinates.");
      TensorProductSetup(coords, std::bitset<dim>(0ULL),0,defaultLoadbalancer());
      init();
    }

     /** @brief Constructor for a tensorproduct YaspGrid
      *  @param comm MPI communicator where this mesh is distributed to
      *  @param coords coordinate vectors to be used for coarse grid
      *  @param periodic tells if direction is periodic or not
      *  @param overlap size of overlap on coarsest grid (same in all directions)
      *  @param lb pointer to an overloaded YLoadBalance instance
      */
    YaspGrid (Dune::MPIHelper::MPICommunicator comm,
              Dune::array<std::vector<ctype>, dim> coords,
              std::bitset<dim> periodic, int overlap,
              const YLoadBalance<dim>* lb = defaultLoadbalancer())
#if HAVE_MPI
      : ccobj(comm), _torus(comm,tag,Dune::Yasp::sizeArray<dim>(coords),defaultLoadbalancer()),
#else
      : _torus(tag,Dune::Yasp::sizeArray(coords),defaultLoadbalancer()),
#endif
        leafIndexSet_(*this),
        _periodic(std::bitset<dim>(0)),
        _overlap(overlap),
        keep_ovlp(true),
        adaptRefCount(0), adaptActive(false)
    {
      if (!Dune::Yasp::checkIfMonotonous(coords))
        DUNE_THROW(Dune::GridError,"Setup of a tensorproduct grid requires monotonous sequences of coordinates.");
      TensorProductSetup(coords, periodic, overlap, lb);
      init();
    }

    /** @brief Constructor for a sequential tensorproduct YaspGrid
     *
     *  Sequential here means that the whole grid is living on one process even if your program is running
     *  in parallel.
     *  @see YaspGrid(Dune::MPIHelper::MPICommunicator, Dune::array<std::vector<ctype>,dim>,  Dune::FieldVector<bool, dim>, int)
     *  for constructing one parallel grid decomposed between the processors.
     *  @param coords coordinate vectors to be used for coarse grids
     *  @param periodic tells if direction is periodic or not
     *  @param overlap size of overlap on coarsest grid (same in all directions)
     *  @param lb pointer to an overloaded YLoadBalance instance
     */
    YaspGrid (Dune::array<std::vector<ctype>, dim> coords,
              std::bitset<dim> periodic, int overlap,
              const YLoadBalance<dim>* lb = defaultLoadbalancer())
#if HAVE_MPI
      : ccobj(MPI_COMM_SELF),
      _torus(MPI_COMM_SELF,tag,Dune::Yasp::sizeArray<dim>(coords),defaultLoadbalancer()),
#else
      : _torus(tag,Dune::Yasp::sizeArray<dim>(coords),defaultLoadbalancer()),
#endif
        leafIndexSet_(*this),
        _periodic(std::bitset<dim>(0)),
        _overlap(overlap),
        keep_ovlp(true),
        adaptRefCount(0), adaptActive(false)
    {
      if (!Dune::Yasp::checkIfMonotonous(coords))
        DUNE_THROW(Dune::GridError,"Setup of a tensorproduct grid requires monotonous sequences of coordinates.");
      TensorProductSetup(coords, periodic, overlap, lb);
      init();
    }

  private:

    /** @brief Constructor for a tensorproduct YaspGrid with only coordinate
     *         information on this processor
     *  @param comm MPI communicator where this mesh is distributed to
     *  @param coords coordinate vectors to be used for coarse grid
     *  @param periodic tells if direction is periodic or not
     *  @param overlap size of overlap on coarsest grid (same in all directions)
     *  @param offset the index offset in a global coordinate vector
     *  @param coarseSize the coarse size of the global grid
     *  @param lb pointer to an overloaded YLoadBalance instance
     *
     *  @warning The construction of overlapping coordinate ranges is
     *           an error-prone procedure. For this reason, it is kept private.
     *           You can safely use it through BackupRestoreFacility. All other
     *           use involves some serious pitfalls.
     */
    YaspGrid (Dune::MPIHelper::MPICommunicator comm,
              Dune::array<std::vector<ctype>, dim> coords,
              std::bitset<dim> periodic,
              int overlap,
              Dune::array<int,dim> coarseSize,
              const YLoadBalance<dim>* lb = defaultLoadbalancer())
    #if HAVE_MPI
    : ccobj(comm), _torus(comm,tag,coarseSize,lb),
    #else
    : _torus(tag,coarseSize,lb),
    #endif
    leafIndexSet_(*this),
    _periodic(std::bitset<dim>(0)),
    _overlap(overlap),
    _coarseSize(coarseSize),
    keep_ovlp(true),
    adaptRefCount(0), adaptActive(false)
    {
      // check whether YaspGrid has been given the correct template parameter
      static_assert(is_same<CoordCont,TensorProductCoordinates<ctype,dim> >::value,
                  "YaspGrid coordinate container template parameter and given constructor values do not match!");

      if (!Dune::Yasp::checkIfMonotonous(coords))
        DUNE_THROW(Dune::GridError,"Setup of a tensorproduct grid requires monotonous sequences of coordinates.");

      _levels.resize(1);

      Dune::array<int,dim> o;
      std::fill(o.begin(), o.end(), 0);
      Dune::array<int,dim> o_interior(o);
      Dune::array<int,dim> s_interior(coarseSize);
#if HAVE_MPI
      double imbal = _torus.partition(_torus.rank(),o,coarseSize,o_interior,s_interior);
#endif

      // get offset by modifying o_interior accoring to overlap
      Dune::array<int,dim> offset(o_interior);
      for (int i=0; i<dim; i++)
        if ((periodic[i]) || (o_interior[i] > 0))
          offset[i] -= overlap;

      TensorProductCoordinates<ctype,dim> cc(coords, offset);

      // add level
      makelevel(cc,periodic,o_interior,overlap);

      init();
    }

    // the backup restore facility needs to be able to use above constructor
    friend class BackupRestoreFacility<YaspGrid<dim,CoordCont> >;

    // do not copy this class
    YaspGrid(const YaspGrid&);

  public:

    /*! Return maximum level defined in this grid. Levels are numbered
          0 ... maxlevel with 0 the coarsest level.
     */
    int maxLevel() const
    {
      return _levels.size()-1;
    }

    //! refine the grid refCount times.
    void globalRefine (int refCount)
    {
      if (refCount < -maxLevel())
        DUNE_THROW(GridError, "Only " << maxLevel() << " levels left. " <<
                   "Coarsening " << -refCount << " levels requested!");

      // If refCount is negative then coarsen the grid
      for (int k=refCount; k<0; k++)
      {
        // create an empty grid level
        YGridLevel empty;
        _levels.back() = empty;
        // reduce maxlevel
        _levels.pop_back();

        indexsets.pop_back();
      }

      // If refCount is positive refine the grid
      for (int k=0; k<refCount; k++)
      {
        // access to coarser grid level
        YGridLevel& cg = _levels[maxLevel()];

        std::bitset<dim> ovlp_low(0ULL), ovlp_up(0ULL);
        for (int i=0; i<dim; i++)
        {
          if (cg.overlap[0].dataBegin()->origin(i) > 0)
            ovlp_low[i] = true;
          if (cg.overlap[0].dataBegin()->max(i) + 1 < globalSize(i))
            ovlp_up[i] = true;
        }

        CoordCont newcont(cg.coords.refine(ovlp_low, ovlp_up, keep_ovlp, cg.overlapSize));

        int overlap = (keep_ovlp) ? 2*cg.overlapSize : cg.overlapSize;

        //determine new origin
        iTupel o_interior;
        for (int i=0; i<dim; i++)
          o_interior[i] = 2*cg.interior[0].dataBegin()->origin(i);

        // add level
        _levels.resize(_levels.size() + 1);
        makelevel(newcont,_periodic,o_interior,overlap);

        indexsets.push_back( make_shared<YaspIndexSet<const YaspGrid<dim,CoordCont>, false > >(*this,maxLevel()) );
      }
    }

    /**
       \brief set options for refinement
       @param keepPhysicalOverlap [true] keep the physical size of the overlap, [false] keep the number of cells in the overlap.  Default is [true].
     */
    void refineOptions (bool keepPhysicalOverlap)
    {
      keep_ovlp = keepPhysicalOverlap;
    }

    /** \brief Marks an entity to be refined/coarsened in a subsequent adapt.

       \param[in] refCount Number of subdivisions that should be applied. Negative value means coarsening.
       \param[in] e        Entity to Entity that should be refined

       \return true if Entity was marked, false otherwise.

       \note
          -  On yaspgrid marking one element will mark all other elements of the level as well
          -  If refCount is lower than refCount of a previous mark-call, nothing is changed
     */
    bool mark( int refCount, const typename Traits::template Codim<0>::Entity & e )
    {
      assert(adaptActive == false);
      if (e.level() != maxLevel()) return false;
      adaptRefCount = std::max(adaptRefCount, refCount);
      return true;
    }

    /** \brief returns adaptation mark for given entity

       \param[in] e   Entity for which adaptation mark should be determined

       \return int adaptation mark, here the default value 0 is returned
     */
    int getMark ( const typename Traits::template Codim<0>::Entity &e ) const
    {
      return ( e.level() == maxLevel() ) ? adaptRefCount : 0;
    }

    //! map adapt to global refine
    bool adapt ()
    {
      globalRefine(adaptRefCount);
      return (adaptRefCount > 0);
    }

    //! returns true, if the grid will be coarsened
    bool preAdapt ()
    {
      adaptActive = true;
      adaptRefCount = comm().max(adaptRefCount);
      return (adaptRefCount < 0);
    }

    //! clean up some markers
    void postAdapt()
    {
      adaptActive = false;
      adaptRefCount = 0;
    }

    //! one past the end on this level
    template<int cd, PartitionIteratorType pitype>
    typename Traits::template Codim<cd>::template Partition<pitype>::LevelIterator lbegin (int level) const
    {
      return levelbegin<cd,pitype>(level);
    }

    //! Iterator to one past the last entity of given codim on level for partition type
    template<int cd, PartitionIteratorType pitype>
    typename Traits::template Codim<cd>::template Partition<pitype>::LevelIterator lend (int level) const
    {
      return levelend<cd,pitype>(level);
    }

    //! version without second template parameter for convenience
    template<int cd>
    typename Traits::template Codim<cd>::template Partition<All_Partition>::LevelIterator lbegin (int level) const
    {
      return levelbegin<cd,All_Partition>(level);
    }

    //! version without second template parameter for convenience
    template<int cd>
    typename Traits::template Codim<cd>::template Partition<All_Partition>::LevelIterator lend (int level) const
    {
      return levelend<cd,All_Partition>(level);
    }

    //! return LeafIterator which points to the first entity in maxLevel
    template<int cd, PartitionIteratorType pitype>
    typename Traits::template Codim<cd>::template Partition<pitype>::LeafIterator leafbegin () const
    {
      return levelbegin<cd,pitype>(maxLevel());
    }

    //! return LeafIterator which points behind the last entity in maxLevel
    template<int cd, PartitionIteratorType pitype>
    typename Traits::template Codim<cd>::template Partition<pitype>::LeafIterator leafend () const
    {
      return levelend<cd,pitype>(maxLevel());
    }

    //! return LeafIterator which points to the first entity in maxLevel
    template<int cd>
    typename Traits::template Codim<cd>::template Partition<All_Partition>::LeafIterator leafbegin () const
    {
      return levelbegin<cd,All_Partition>(maxLevel());
    }

    //! return LeafIterator which points behind the last entity in maxLevel
    template<int cd>
    typename Traits::template Codim<cd>::template Partition<All_Partition>::LeafIterator leafend () const
    {
      return levelend<cd,All_Partition>(maxLevel());
    }

    // \brief obtain EntityPointer from EntitySeed. */
    template <typename Seed>
    typename Traits::template Codim<Seed::codimension>::EntityPointer
    entityPointer(const Seed& seed) const
    {
      const int codim = Seed::codimension;
      YGridLevelIterator g = begin(this->getRealImplementation(seed).level());

      return YaspEntityPointer<codim,GridImp>(this,g,
        typename YGrid::Iterator(g->overlapfront[codim], this->getRealImplementation(seed).coord(),this->getRealImplementation(seed).offset()));
    }

    //! return size (= distance in graph) of overlap region
    int overlapSize (int level, int codim) const
    {
      YGridLevelIterator g = begin(level);
      return g->overlapSize;
    }

    //! return size (= distance in graph) of overlap region
    int overlapSize (int codim) const
    {
      YGridLevelIterator g = begin(maxLevel());
      return g->overlapSize;
    }

    //! return size (= distance in graph) of ghost region
    int ghostSize (int level, int codim) const
    {
      return 0;
    }

    //! return size (= distance in graph) of ghost region
    int ghostSize (int codim) const
    {
      return 0;
    }

    //! number of entities per level and codim in this process
    int size (int level, int codim) const
    {
      YGridLevelIterator g = begin(level);

      // sum over all components of the codimension
      int count = 0;
      typedef typename Dune::array<YGridComponent<CoordCont>, StaticPower<2,dim>::power>::iterator DAI;
      for (DAI it = g->overlapfront[codim].dataBegin(); it != g->overlapfront[codim].dataEnd(); ++it)
        count += it->totalsize();

      return count;
    }

    //! number of leaf entities per codim in this process
    int size (int codim) const
    {
      return size(maxLevel(),codim);
    }

    //! number of entities per level and geometry type in this process
    int size (int level, GeometryType type) const
    {
      return (type.isCube()) ? size(level,dim-type.dim()) : 0;
    }

    //! number of leaf entities per geometry type in this process
    int size (GeometryType type) const
    {
      return size(maxLevel(),type);
    }

    //! \brief returns the number of boundary segments within the macro grid
    size_t numBoundarySegments () const
    {
      return nBSegments;
    }

    /*! The new communication interface

       communicate objects for all codims on a given level
     */
    template<class DataHandleImp, class DataType>
    void communicate (CommDataHandleIF<DataHandleImp,DataType> & data, InterfaceType iftype, CommunicationDirection dir, int level) const
    {
      YaspCommunicateMeta<dim,dim>::comm(*this,data,iftype,dir,level);
    }

    /*! The new communication interface

       communicate objects for all codims on the leaf grid
     */
    template<class DataHandleImp, class DataType>
    void communicate (CommDataHandleIF<DataHandleImp,DataType> & data, InterfaceType iftype, CommunicationDirection dir) const
    {
      YaspCommunicateMeta<dim,dim>::comm(*this,data,iftype,dir,this->maxLevel());
    }

    /*! The new communication interface

       communicate objects for one codim
     */
    template<class DataHandle, int codim>
    void communicateCodim (DataHandle& data, InterfaceType iftype, CommunicationDirection dir, int level) const
    {
      // check input
      if (!data.contains(dim,codim)) return; // should have been checked outside

      // data types
      typedef typename DataHandle::DataType DataType;

      // access to grid level
      YGridLevelIterator g = begin(level);

      // find send/recv lists or throw error
      const YGridList<CoordCont>* sendlist = 0;
      const YGridList<CoordCont>* recvlist = 0;

      if (iftype==InteriorBorder_InteriorBorder_Interface)
      {
        sendlist = &g->send_interiorborder_interiorborder[codim];
        recvlist = &g->recv_interiorborder_interiorborder[codim];
      }
      if (iftype==InteriorBorder_All_Interface)
      {
        sendlist = &g->send_interiorborder_overlapfront[codim];
        recvlist = &g->recv_overlapfront_interiorborder[codim];
      }
      if (iftype==Overlap_OverlapFront_Interface || iftype==Overlap_All_Interface)
      {
        sendlist = &g->send_overlap_overlapfront[codim];
        recvlist = &g->recv_overlapfront_overlap[codim];
      }
      if (iftype==All_All_Interface)
      {
        sendlist = &g->send_overlapfront_overlapfront[codim];
        recvlist = &g->recv_overlapfront_overlapfront[codim];
      }

      // change communication direction?
      if (dir==BackwardCommunication)
        std::swap(sendlist,recvlist);

      int cnt;

      // Size computation (requires communication if variable size)
      std::vector<int> send_size(sendlist->size(),-1);    // map rank to total number of objects (of type DataType) to be sent
      std::vector<int> recv_size(recvlist->size(),-1);    // map rank to total number of objects (of type DataType) to be recvd
      std::vector<size_t*> send_sizes(sendlist->size(),static_cast<size_t*>(0)); // map rank to array giving number of objects per entity to be sent
      std::vector<size_t*> recv_sizes(recvlist->size(),static_cast<size_t*>(0)); // map rank to array giving number of objects per entity to be recvd

      // define type to iterate over send and recv lists
      typedef typename YGridList<CoordCont>::Iterator ListIt;

      if (data.fixedsize(dim,codim))
      {
        // fixed size: just take a dummy entity, size can be computed without communication
        cnt=0;
        for (ListIt is=sendlist->begin(); is!=sendlist->end(); ++is)
        {
          typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
          it(YaspLevelIterator<codim,All_Partition,GridImp>(this, g, typename YGrid::Iterator(is->yg)));
          send_size[cnt] = is->grid.totalsize() * data.size(*it);
          cnt++;
        }
        cnt=0;
        for (ListIt is=recvlist->begin(); is!=recvlist->end(); ++is)
        {
          typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
          it(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg)));
          recv_size[cnt] = is->grid.totalsize() * data.size(*it);
          cnt++;
        }
      }
      else
      {
        // variable size case: sender side determines the size
        cnt=0;
        for (ListIt is=sendlist->begin(); is!=sendlist->end(); ++is)
        {
          // allocate send buffer for sizes per entitiy
          size_t *buf = new size_t[is->grid.totalsize()];
          send_sizes[cnt] = buf;

          // loop over entities and ask for size
          int i=0; size_t n=0;
          typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
          it(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg)));
          typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
          itend(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg,true)));
          for ( ; it!=itend; ++it)
          {
            buf[i] = data.size(*it);
            n += buf[i];
            i++;
          }

          // now we know the size for this rank
          send_size[cnt] = n;

          // hand over send request to torus class
          torus().send(is->rank,buf,is->grid.totalsize()*sizeof(size_t));
          cnt++;
        }

        // allocate recv buffers for sizes and store receive request
        cnt=0;
        for (ListIt is=recvlist->begin(); is!=recvlist->end(); ++is)
        {
          // allocate recv buffer
          size_t *buf = new size_t[is->grid.totalsize()];
          recv_sizes[cnt] = buf;

          // hand over recv request to torus class
          torus().recv(is->rank,buf,is->grid.totalsize()*sizeof(size_t));
          cnt++;
        }

        // exchange all size buffers now
        torus().exchange();

        // release send size buffers
        cnt=0;
        for (ListIt is=sendlist->begin(); is!=sendlist->end(); ++is)
        {
          delete[] send_sizes[cnt];
          send_sizes[cnt] = 0;
          cnt++;
        }

        // process receive size buffers
        cnt=0;
        for (ListIt is=recvlist->begin(); is!=recvlist->end(); ++is)
        {
          // get recv buffer
          size_t *buf = recv_sizes[cnt];

          // compute total size
          size_t n=0;
          for (int i=0; i<is->grid.totalsize(); ++i)
            n += buf[i];

          // ... and store it
          recv_size[cnt] = n;
          ++cnt;
        }
      }


      // allocate & fill the send buffers & store send request
      std::vector<DataType*> sends(sendlist->size(), static_cast<DataType*>(0)); // store pointers to send buffers
      cnt=0;
      for (ListIt is=sendlist->begin(); is!=sendlist->end(); ++is)
      {
        // allocate send buffer
        DataType *buf = new DataType[send_size[cnt]];

        // remember send buffer
        sends[cnt] = buf;

        // make a message buffer
        MessageBuffer<DataType> mb(buf);

        // fill send buffer; iterate over cells in intersection
        typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
        it(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg)));
        typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
        itend(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg,true)));
        for ( ; it!=itend; ++it)
          data.gather(mb,*it);

        // hand over send request to torus class
        torus().send(is->rank,buf,send_size[cnt]*sizeof(DataType));
        cnt++;
      }

      // allocate recv buffers and store receive request
      std::vector<DataType*> recvs(recvlist->size(),static_cast<DataType*>(0)); // store pointers to send buffers
      cnt=0;
      for (ListIt is=recvlist->begin(); is!=recvlist->end(); ++is)
      {
        // allocate recv buffer
        DataType *buf = new DataType[recv_size[cnt]];

        // remember recv buffer
        recvs[cnt] = buf;

        // hand over recv request to torus class
        torus().recv(is->rank,buf,recv_size[cnt]*sizeof(DataType));
        cnt++;
      }

      // exchange all buffers now
      torus().exchange();

      // release send buffers
      cnt=0;
      for (ListIt is=sendlist->begin(); is!=sendlist->end(); ++is)
      {
        delete[] sends[cnt];
        sends[cnt] = 0;
        cnt++;
      }

      // process receive buffers and delete them
      cnt=0;
      for (ListIt is=recvlist->begin(); is!=recvlist->end(); ++is)
      {
        // get recv buffer
        DataType *buf = recvs[cnt];

        // make a message buffer
        MessageBuffer<DataType> mb(buf);

        // copy data from receive buffer; iterate over cells in intersection
        if (data.fixedsize(dim,codim))
        {
          typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
          it(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg)));
          size_t n=data.size(*it);
          typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
          itend(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg,true)));
          for ( ; it!=itend; ++it)
            data.scatter(mb,*it,n);
        }
        else
        {
          int i=0;
          size_t *sbuf = recv_sizes[cnt];
          typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
          it(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg)));
          typename Traits::template Codim<codim>::template Partition<All_Partition>::LevelIterator
          itend(YaspLevelIterator<codim,All_Partition,GridImp>(this,g, typename YGrid::Iterator(is->yg,true)));
          for ( ; it!=itend; ++it)
            data.scatter(mb,*it,sbuf[i++]);
          delete[] sbuf;
        }

        // delete buffer
        delete[] buf; // hier krachts !
        cnt++;
      }
    }

    // The new index sets from DDM 11.07.2005
    const typename Traits::GlobalIdSet& globalIdSet() const
    {
      return theglobalidset;
    }

    const typename Traits::LocalIdSet& localIdSet() const
    {
      return theglobalidset;
    }

    const typename Traits::LevelIndexSet& levelIndexSet(int level) const
    {
      if (level<0 || level>maxLevel()) DUNE_THROW(RangeError, "level out of range");
      return *(indexsets[level]);
    }

    const typename Traits::LeafIndexSet& leafIndexSet() const
    {
      return leafIndexSet_;
    }

#if HAVE_MPI
    /*! @brief return a collective communication object
     */
    const CollectiveCommunication<MPI_Comm>& comm () const
    {
      return ccobj;
    }
#else
    /*! @brief return a collective communication object
     */
    const CollectiveCommunication<YaspGrid>& comm () const
    {
      return ccobj;
    }
#endif

  private:

    // number of boundary segments of the level 0 grid
    int nBSegments;

    // Index classes need access to the real entity
    friend class Dune::YaspIndexSet<const Dune::YaspGrid<dim, CoordCont>, true >;
    friend class Dune::YaspIndexSet<const Dune::YaspGrid<dim, CoordCont>, false >;
    friend class Dune::YaspGlobalIdSet<const Dune::YaspGrid<dim, CoordCont> >;

    friend class Dune::YaspIntersectionIterator<const Dune::YaspGrid<dim, CoordCont> >;
    friend class Dune::YaspIntersection<const Dune::YaspGrid<dim, CoordCont> >;
    friend class Dune::YaspEntity<0, dim, const Dune::YaspGrid<dim, CoordCont> >;

    template <int codim_, class GridImp_>
    friend class Dune::YaspEntityPointer;

    template<int codim_, int dim_, class GridImp_, template<int,int,class> class EntityImp_>
    friend class Entity;

    template<class DT>
    class MessageBuffer {
    public:
      // Constructor
      MessageBuffer (DT *p)
      {
        a=p;
        i=0;
        j=0;
      }

      // write data to message buffer, acts like a stream !
      template<class Y>
      void write (const Y& data)
      {
        static_assert(( is_same<DT,Y>::value ), "DataType mismatch");
        a[i++] = data;
      }

      // read data from message buffer, acts like a stream !
      template<class Y>
      void read (Y& data) const
      {
        static_assert(( is_same<DT,Y>::value ), "DataType mismatch");
        data = a[j++];
      }

    private:
      DT *a;
      int i;
      mutable int j;
    };

    //! one past the end on this level
    template<int cd, PartitionIteratorType pitype>
    YaspLevelIterator<cd,pitype,GridImp> levelbegin (int level) const
    {
      YGridLevelIterator g = begin(level);
      if (level<0 || level>maxLevel()) DUNE_THROW(RangeError, "level out of range");

      if (pitype==Interior_Partition)
        return YaspLevelIterator<cd,pitype,GridImp>(this,g,g->interior[cd].begin());
      if (pitype==InteriorBorder_Partition)
        return YaspLevelIterator<cd,pitype,GridImp>(this,g,g->interiorborder[cd].begin());
      if (pitype==Overlap_Partition)
        return YaspLevelIterator<cd,pitype,GridImp>(this,g,g->overlap[cd].begin());
      if (pitype<=All_Partition)
        return YaspLevelIterator<cd,pitype,GridImp>(this,g,g->overlapfront[cd].begin());
      if (pitype==Ghost_Partition)
        return levelend <cd, pitype> (level);

      DUNE_THROW(GridError, "YaspLevelIterator with this codim or partition type not implemented");
    }

    //! Iterator to one past the last entity of given codim on level for partition type
    template<int cd, PartitionIteratorType pitype>
    YaspLevelIterator<cd,pitype,GridImp> levelend (int level) const
    {
      YGridLevelIterator g = begin(level);
      if (level<0 || level>maxLevel()) DUNE_THROW(RangeError, "level out of range");

      if (pitype==Interior_Partition)
        return YaspLevelIterator<cd,pitype,GridImp>(this,g,g->interior[cd].end());
      if (pitype==InteriorBorder_Partition)
        return YaspLevelIterator<cd,pitype,GridImp>(this,g,g->interiorborder[cd].end());
      if (pitype==Overlap_Partition)
        return YaspLevelIterator<cd,pitype,GridImp>(this,g,g->overlap[cd].end());
      if (pitype<=All_Partition || pitype == Ghost_Partition)
        return YaspLevelIterator<cd,pitype,GridImp>(this,g,g->overlapfront[cd].end());

      DUNE_THROW(GridError, "YaspLevelIterator with this codim or partition type not implemented");
    }

#if HAVE_MPI
    CollectiveCommunication<MPI_Comm> ccobj;
#else
    CollectiveCommunication<YaspGrid> ccobj;
#endif

    Torus<dim> _torus;

    std::vector< shared_ptr< YaspIndexSet<const YaspGrid<dim,CoordCont>, false > > > indexsets;
    YaspIndexSet<const YaspGrid<dim,CoordCont>, true> leafIndexSet_;
    YaspGlobalIdSet<const YaspGrid<dim,CoordCont> > theglobalidset;

    fTupel _LL;
    iTupel _s;
    std::bitset<dim> _periodic;
    iTupel _coarseSize;
    ReservedVector<YGridLevel,32> _levels;
    int _overlap;
    bool keep_ovlp;
    int adaptRefCount;
    bool adaptActive;
  };

  //! Output operator for multigrids

  template <int d, class CC>
  inline std::ostream& operator<< (std::ostream& s, YaspGrid<d,CC>& grid)
  {
    int rank = grid.torus().rank();

    s << "[" << rank << "]:" << " YaspGrid maxlevel=" << grid.maxLevel() << std::endl;

    s << "Printing the torus: " <<std::endl;
    s << grid.torus() << std::endl;

    for (typename YaspGrid<d,CC>::YGridLevelIterator g=grid.begin(); g!=grid.end(); ++g)
    {
      s << "[" << rank << "]:   " << std::endl;
      s << "[" << rank << "]:   " << "==========================================" << std::endl;
      s << "[" << rank << "]:   " << "level=" << g->level() << std::endl;

      for (int codim = 0; codim < d + 1; ++codim)
      {
        s << "[" << rank << "]:   " << "overlapfront[" << codim << "]:    " << g->overlapfront[codim] << std::endl;
        s << "[" << rank << "]:   " << "overlap[" << codim << "]:    " << g->overlap[codim] << std::endl;
        s << "[" << rank << "]:   " << "interiorborder[" << codim << "]:    " << g->interiorborder[codim] << std::endl;
        s << "[" << rank << "]:   " << "interior[" << codim << "]:    " << g->interior[codim] << std::endl;

        typedef typename YGridList<CC>::Iterator I;
        for (I i=g->send_overlapfront_overlapfront[codim].begin();
                 i!=g->send_overlapfront_overlapfront[codim].end(); ++i)
          s << "[" << rank << "]:    " << " s_of_of[" << codim << "] to rank "
                   << i->rank << " " << i->grid << std::endl;

        for (I i=g->recv_overlapfront_overlapfront[codim].begin();
                 i!=g->recv_overlapfront_overlapfront[codim].end(); ++i)
          s << "[" << rank << "]:    " << " r_of_of[" << codim << "] to rank "
                   << i->rank << " " << i->grid << std::endl;

        for (I i=g->send_overlap_overlapfront[codim].begin();
                 i!=g->send_overlap_overlapfront[codim].end(); ++i)
          s << "[" << rank << "]:    " << " s_o_of[" << codim << "] to rank "
                   << i->rank << " " << i->grid << std::endl;

        for (I i=g->recv_overlapfront_overlap[codim].begin();
                 i!=g->recv_overlapfront_overlap[codim].end(); ++i)
          s << "[" << rank << "]:    " << " r_of_o[" << codim << "] to rank "
                   << i->rank << " " << i->grid << std::endl;

        for (I i=g->send_interiorborder_interiorborder[codim].begin();
                 i!=g->send_interiorborder_interiorborder[codim].end(); ++i)
          s << "[" << rank << "]:    " << " s_ib_ib[" << codim << "] to rank "
          << i->rank << " " << i->grid << std::endl;

        for (I i=g->recv_interiorborder_interiorborder[codim].begin();
                 i!=g->recv_interiorborder_interiorborder[codim].end(); ++i)
             s << "[" << rank << "]:    " << " r_ib_ib[" << codim << "] to rank "
             << i->rank << " " << i->grid << std::endl;

        for (I i=g->send_interiorborder_overlapfront[codim].begin();
                 i!=g->send_interiorborder_overlapfront[codim].end(); ++i)
             s << "[" << rank << "]:    " << " s_ib_of[" << codim << "] to rank "
             << i->rank << " " << i->grid << std::endl;

        for (I i=g->recv_overlapfront_interiorborder[codim].begin();
                 i!=g->recv_overlapfront_interiorborder[codim].end(); ++i)
             s << "[" << rank << "]:    " << " r_of_ib[" << codim << "] to rank "
             << i->rank << " " << i->grid << std::endl;
      }
    }

    s << std::endl;

    return s;
  }

  namespace Capabilities
  {

    /** \struct hasEntity
       \ingroup YaspGrid
     */

    /** \struct hasBackupRestoreFacilities
       \ingroup YaspGrid
     */

    /** \brief YaspGrid has only one geometry type for codim 0 entities
       \ingroup YaspGrid
     */
    template<int dim, class CoordCont>
    struct hasSingleGeometryType< YaspGrid<dim, CoordCont> >
    {
      static const bool v = true;
      static const unsigned int topologyId = GenericGeometry :: CubeTopology< dim > :: type :: id ;
    };

    /** \brief YaspGrid is a Cartesian grid
        \ingroup YaspGrid
     */
    template<int dim, class CoordCont>
    struct isCartesian< YaspGrid<dim, CoordCont> >
    {
      static const bool v = true;
    };

    /** \brief YaspGrid has entities for all codimensions
       \ingroup YaspGrid
     */
    template<int dim, class CoordCont, int codim>
    struct hasEntity< YaspGrid<dim, CoordCont>, codim>
    {
      static const bool v = true;
    };

    /** \brief YaspGrid can communicate on all codimensions
     *  \ingroup YaspGrid
     */
    template<int dim, int codim, class CoordCont>
    struct canCommunicate< YaspGrid< dim, CoordCont>, codim >
    {
      static const bool v = true;
    };

    /** \brief YaspGrid is parallel
       \ingroup YaspGrid
     */
    template<int dim, class CoordCont>
    struct isParallel< YaspGrid<dim, CoordCont> >
    {
      static const bool v = true;
    };

    /** \brief YaspGrid is levelwise conforming
       \ingroup YaspGrid
     */
    template<int dim, class CoordCont>
    struct isLevelwiseConforming< YaspGrid<dim, CoordCont> >
    {
      static const bool v = true;
    };

    /** \brief YaspGrid is leafwise conforming
       \ingroup YaspGrid
     */
    template<int dim, class CoordCont>
    struct isLeafwiseConforming< YaspGrid<dim, CoordCont> >
    {
      static const bool v = true;
    };

  }

} // end namespace


#endif
