// -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi: set et ts=4 sw=2 sts=2:
#ifndef DUNE_ALBERTA_TREEITERATOR_CC
#define DUNE_ALBERTA_TREEITERATOR_CC

#include <dune/grid/albertagrid/treeiterator.cc>

namespace Dune
{

  // AlbertaMarkerVector
  // -------------------

  template< int dim, int dimworld >
  template< int codim >
  inline bool AlbertaMarkerVector< dim, dimworld >
  ::subEntityOnElement ( const ElementInfo &elementInfo, int subEntity ) const
  {
    assert( marker_[ codim ].size() > 0 );

    const int index = hIndexSet_->template subIndex< 0 >( elementInfo, 0 );
    const int subIndex = hIndexSet_->template subIndex< codim >( elementInfo, subEntity );
    return (marker_[ codim ][ subIndex ] == index);
  }


  template< int dim, int dimworld >
  template< int firstCodim, class Iterator >
  inline void AlbertaMarkerVector< dim, dimworld >
  ::markSubEntities ( const Iterator &begin, const Iterator &end )
  {
    for( int codim = firstCodim; codim <= dimension; ++codim )
    {
      const size_t size = hIndexSet_->size( codim );
      std::vector< int > &vec = marker_[ codim ];
      vec.resize( size );
      for( size_t i = 0; i < size; ++i )
        vec[ i ] = -1;
    }

    for( Iterator it = begin; it != end; ++it )
    {
      const ElementInfo &elementInfo = Grid::getRealImplementation( *it ).elementInfo();
      Alberta::ForLoop< MarkSubEntities, firstCodim, dimension >
      ::apply( *hIndexSet_, marker_, elementInfo );
    }

    up2Date_ = true;
  }



  template< int dim, int dimworld >
  inline void AlbertaMarkerVector< dim, dimworld >::print ( std::ostream &out ) const
  {
    for( int codim = 1; codim <= dimension; ++codim )
    {
      std::vector< int > &marker = marker_[ codim ];
      const int size = marker.size();
      if( size > 0)
      {
        out << std::endl;
        out << "Codimension " << codim << " (" << size << " entries)" << std::endl;
        for( int i = 0; i < size; ++i )
          out << "subentity " << i << " visited on Element " << marker[ i ] << std::endl;
      }
    }
  }



  // AlbertaMarkerVector::MarkSubEntities
  // ------------------------------------

  template< int dim, int dimworld >
  template< int codim >
  class AlbertaMarkerVector< dim, dimworld >::MarkSubEntities
  {
    static const int numSubEntities = Alberta::NumSubEntities< dimension, codim >::value;

    typedef Alberta::ElementInfo< dimension > ElementInfo;

  public:
    template< class Array >
    static void apply ( const HierarchicIndexSet &hIndexSet,
                        Array (&marker)[ dimension + 1],
                        const ElementInfo &elementInfo )
    {
      Array &vec = marker[ codim ];

      const int index = hIndexSet.template subIndex< 0 >( elementInfo, 0 );
      for( int i = 0; i < numSubEntities; ++i )
      {
        const int subIndex = hIndexSet.template subIndex< codim >( elementInfo, i );
        if( vec[ subIndex ] < 0 )
          vec[ subIndex ] = index;
      }
    }
  };



  // AlbertaTreeIterratorHelp
  // ------------------------

  namespace AlbertaTreeIteratorHelp
  {

    // for elements
    template< class IteratorImp, int dim >
    struct GoNextEntity< IteratorImp, dim, 0 >
    {
      typedef typename IteratorImp::ElementInfo ElementInfo;

      static void goNext ( IteratorImp &it, ElementInfo &elementInfo )
      {
        it.goNextElement( elementInfo );
      }
    };

    // for faces
    template <class IteratorImp, int dim>
    struct GoNextEntity<IteratorImp,dim,1>
    {
      typedef typename IteratorImp::ElementInfo ElementInfo;

      static void goNext ( IteratorImp &it, ElementInfo &elementInfo )
      {
        it.goNextFace( elementInfo );
      }
    };

    // for vertices
    template <class IteratorImp, int dim>
    struct GoNextEntity<IteratorImp,dim,dim>
    {
      typedef typename IteratorImp::ElementInfo ElementInfo;

      static void goNext ( IteratorImp &it, ElementInfo &elementInfo )
      {
        it.goNextVertex( elementInfo );
      }
    };

    // for edges in 3d
    template <class IteratorImp>
    struct GoNextEntity<IteratorImp,3,2>
    {
      typedef typename IteratorImp::ElementInfo ElementInfo;

      static void goNext ( IteratorImp &it, ElementInfo &elementInfo )
      {
        it.goNextEdge( elementInfo );
      }
    };

  } // end namespace AlbertaTreeIteratorHelp



  // AlbertaGridTreeIterator
  // -----------------------

  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::goNextEntity ( ElementInfo &elementInfo )
  {
    return AlbertaTreeIteratorHelp::GoNextEntity< This, GridImp::dimension, codim >
           ::goNext( *this, elementInfo );
  }


  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >::makeIterator ()
  {
    level_ = 0;
    subEntity_ = -1;
    vertexMarker_ = 0;

    entityImp().clearElement();
  }


  template< int codim, class GridImp, bool leafIterator >
  inline AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::AlbertaGridTreeIterator ( const GridImp &grid,
                              const MarkerVector *vertexMark,
                              int travLevel )
    : Base( grid ),
      level_( travLevel ),
      subEntity_( (codim == 0 ? 0 : -1) ),
      macroIterator_( grid.meshPointer().begin() ),
      vertexMarker_( vertexMark )
  {
    ElementInfo elementInfo = *macroIterator_;
    if( codim == 0 )
      nextElementStop( elementInfo );
    else
      goNextEntity( elementInfo );
    // it is ok to set the invalid ElementInfo
    entityImp().setElement( elementInfo, subEntity_ );
  }


  // Make LevelIterator with point to element from previous iterations
  template< int codim, class GridImp, bool leafIterator >
  inline AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::AlbertaGridTreeIterator ( const GridImp &grid,
                              int travLevel )
    : Base( grid ),
      level_( travLevel ),
      subEntity_( -1 ),
      macroIterator_( grid.meshPointer().end() ),
      vertexMarker_( 0 )
  {}


  // Make LevelIterator with point to element from previous iterations
  template< int codim, class GridImp, bool leafIterator >
  inline AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::AlbertaGridTreeIterator( const This &other )
    : Base( other ),
      level_( other.level_ ),
      subEntity_( other.subEntity_ ),
      macroIterator_( other.macroIterator_ ),
      vertexMarker_( other.vertexMarker_ )
  {}


  // Make LevelIterator with point to element from previous iterations
  template< int codim, class GridImp, bool leafIterator >
  inline typename AlbertaGridTreeIterator< codim, GridImp, leafIterator >::This &
  AlbertaGridTreeIterator< codim, GridImp, leafIterator >::operator= ( const This &other )
  {
    Base::operator=( other );

    level_ = other.level_;
    subEntity_ =  other.subEntity_;
    macroIterator_ = other.macroIterator_;
    vertexMarker_ = other.vertexMarker_;

    return *this;
  }


  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >::increment ()
  {
    ElementInfo elementInfo = entityImp().elementInfo_;
    goNextEntity ( elementInfo );
    // it is ok to set the invalid ElementInfo
    entityImp().setElement( elementInfo, subEntity_ );
  }


  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::nextElement ( ElementInfo &elementInfo )
  {
    if( elementInfo.isLeaf() )
    {
      while( (elementInfo.level() > 0) && (elementInfo.indexInFather() == 1) )
        elementInfo = elementInfo.father();
      if( elementInfo.level() == 0 )
      {
        ++macroIterator_;
        elementInfo = *macroIterator_;
      }
      else
        elementInfo = elementInfo.father().child( 1 );
    }
    else
      elementInfo = elementInfo.child( 0 );
  }


  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::nextElementStop ( ElementInfo &elementInfo )
  {
    while( !(!elementInfo || stopAtElement( elementInfo )) )
      nextElement( elementInfo );
  }


  template< int codim, class GridImp, bool leafIterator >
  inline bool AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::stopAtElement ( const ElementInfo &elementInfo )
  {
    if( !elementInfo )
      return true;
    return (leafIterator ? elementInfo.isLeaf() : (level_ == elementInfo.level()));
  }


  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::goNextElement ( ElementInfo &elementInfo )
  {
    nextElement( elementInfo );
    nextElementStop( elementInfo );
  }


  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::goNextFace ( ElementInfo &elementInfo )
  {
    ++subEntity_;
    if( subEntity_ >= numSubEntities )
    {
      subEntity_ = 0;
      nextElement( elementInfo );
      nextElementStop( elementInfo );
      if( !elementInfo )
        return;
    }

    if( leafIterator )
    {
      const ALBERTA EL *neighbor = elementInfo.elInfo().neigh[ subEntity_ ];
      if( neighbor != NULL )
      {
        // face is reached from element with largest number
        const HierarchicIndexSet &hIndexSet = grid().hierarchicIndexSet();
        const int elIndex = hIndexSet.template subIndex< 0 >( elementInfo, 0 );
        const int nbIndex = hIndexSet.template subIndex< 0 >( neighbor, 0 );
        if( elIndex < nbIndex )
          goNextFace( elementInfo );
      }
    }
    else
    {
      assert( vertexMarker_ != 0 );
      if( !vertexMarker_->template subEntityOnElement< 1 >( elementInfo, subEntity_ ) )
        goNextFace( elementInfo );
    }
  }


  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::goNextEdge ( ElementInfo &elementInfo )
  {
    ++subEntity_;
    if( subEntity_ >= numSubEntities )
    {
      subEntity_ = 0;
      nextElement( elementInfo );
      nextElementStop( elementInfo );
      if( !elementInfo )
        return;
    }

    assert( vertexMarker_ != 0 );
    if( !vertexMarker_->template subEntityOnElement< 2 >( elementInfo, subEntity_ ) )
      goNextEdge( elementInfo );
  }


  template< int codim, class GridImp, bool leafIterator >
  inline void AlbertaGridTreeIterator< codim, GridImp, leafIterator >
  ::goNextVertex ( ElementInfo &elementInfo )
  {
    ++subEntity_;
    if( subEntity_ >= numSubEntities )
    {
      subEntity_ = 0;
      nextElement( elementInfo );
      nextElementStop( elementInfo );
      if( !elementInfo )
        return;
    }

    assert( vertexMarker_ != 0 );
    if( !vertexMarker_->template subEntityOnElement< dimension >( elementInfo, subEntity_ ) )
      goNextVertex( elementInfo );
  }

}

#endif
