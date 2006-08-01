// -*- tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi: set et ts=4 sw=2 sts=2:
#ifndef DUNE_ONE_D_GRID_INTERSECTIONIT_HH
#define DUNE_ONE_D_GRID_INTERSECTIONIT_HH

/** \file
 * \brief The OneDGridIntersectionIterator class
 */

namespace Dune {


  //**********************************************************************
  //
  // --OneDGridIntersectionIterator
  // --IntersectionIterator
  /** \brief Iterator over all element neighbors
   * \ingroup OneDGrid
     Mesh entities of codimension 0 ("elements") allow to visit all neighbors, where
     a neighbor is an entity of codimension 0 which has a common entity of codimension
     These neighbors are accessed via a IntersectionIterator. This allows the implement
     non-matching meshes. The number of neigbors may be different from the number
     of an element!
   */
  template<class GridImp>
  class OneDGridIntersectionIterator :
    public IntersectionIteratorDefaultImplementation <GridImp, OneDGridIntersectionIterator>
  {
    enum { dim=GridImp::dimension };
    enum { dimworld=GridImp::dimensionworld };

    friend class OneDGridEntity<0,dim,GridImp>;

    //! Constructor for a given grid entity and a given neighbor
    OneDGridIntersectionIterator(OneDEntityImp<1>* center, int nb) : center_(center), neighbor_(nb)
    {}

    /** \brief Constructor creating the 'one-after-last'-iterator */
    OneDGridIntersectionIterator(OneDEntityImp<1>* center) : center_(center), neighbor_(4)
    {}

  public:

    typedef typename GridImp::template Codim<1>::Geometry Geometry;
    typedef typename GridImp::template Codim<1>::LocalGeometry LocalGeometry;
    typedef typename GridImp::template Codim<0>::EntityPointer EntityPointer;
    typedef typename GridImp::template Codim<0>::Entity Entity;

    //! The Destructor
    ~OneDGridIntersectionIterator() {};

    //! equality
    bool equals(const OneDGridIntersectionIterator<GridImp>& other) const {
      return (center_ == other.center_) && (neighbor_ == other.neighbor_);
    }

    //! prefix increment
    void increment() {

      neighbor_++;

      if (neighbor_==2) {
        if (!center_->isLeaf())
          // Skip next leaf intersection, because inside is not a leaf element
          neighbor_++;
        else if (boundary())
          // Skip next leaf intersection because it is a boundary intersection
          // and therefore coincides with the level intersection
          neighbor_++;
        else if (center_->pred_!=NULL
                 && center_->pred_->vertex_[1] == center_->vertex_[0]
                 && center_->pred_->isLeaf())
          // Skip next leaf intersection because it coincides with a level intersection
          neighbor_++;

      }

      if (neighbor_==3) {
        if (!center_->isLeaf())
          // Skip next leaf intersection, because inside is not a leaf element
          neighbor_++;
        else if (boundary())
          // Skip next leaf intersection because it is a boundary intersection
          // and therefore coincides with the level intersection
          neighbor_++;
        else if (center_->succ_!=NULL
                 && center_->succ_->vertex_[0] == center_->vertex_[1]
                 && center_->succ_->isLeaf())
          // Skip next leaf intersection because it coincides with a level intersection
          neighbor_++;

      }

    }

    OneDEntityImp<1>* target() const {
      const bool isValid = center_ && neighbor_>=0 && neighbor_<4;

      if (!isValid)
        return center_;
      else if (neighbor_==0)
        return center_->pred_;
      else if (neighbor_==1)
        return center_->succ_;
      else if (neighbor_==2) {

        // Get left leaf neighbor
        if (center_->pred_ && center_->pred_->vertex_[1] == center_->vertex_[0]) {

          OneDEntityImp<1>* leftLeafNeighbor = center_->pred_;
          while (!leftLeafNeighbor->isLeaf()) {
            assert (leftLeafNeighbor->sons_[1] != NULL);
            leftLeafNeighbor = leftLeafNeighbor->sons_[1];
          }
          return leftLeafNeighbor;

        } else {

          OneDEntityImp<1>* ancestor = center_;
          while (ancestor->father_) {
            ancestor = ancestor->father_;
            if (ancestor->pred_ && ancestor->pred_->vertex_[1] == ancestor->vertex_[0]) {
              assert(ancestor->pred_->isLeaf());
              return ancestor->pred_;
            }
          }

          DUNE_THROW(GridError, "Programming error, apparently we're on the left boundary, neighbor_==2 should not occur!");
        }

      } else {

        // Get right leaf neighbor
        if (center_->succ_ && center_->succ_->vertex_[0] == center_->vertex_[1]) {

          OneDEntityImp<1>* rightLeafNeighbor = center_->succ_;
          while (!rightLeafNeighbor->isLeaf()) {
            assert (rightLeafNeighbor->sons_[0] != NULL);
            rightLeafNeighbor = rightLeafNeighbor->sons_[0];
          }
          return rightLeafNeighbor;

        } else {

          OneDEntityImp<1>* ancestor = center_;
          while (ancestor->father_) {
            ancestor = ancestor->father_;
            if (ancestor->succ_ && ancestor->succ_->vertex_[0] == ancestor->vertex_[1]) {
              assert(ancestor->succ_->isLeaf());
              return ancestor->succ_;
            }
          }

          DUNE_THROW(GridError, "Programming error, apparently we're on the right boundary, neighbor_==3 should not occur!");
        }

      }

    }

    //! return true if intersection is with boundary.
    bool boundary () const {

      // Check whether we're on the left boundary
      if ((neighbor_%2)==0) {

        // If there's an element to the left we can't be on the boundary
        if (center_->pred_)
          return false;

        const OneDEntityImp<1>* ancestor = center_;

        while (ancestor->level_!=0) {

          // Check if we're the left son of our father
          if (ancestor != ancestor->father_->sons_[0])
            return false;

          ancestor = ancestor->father_;
        }

        // We have reached level 0.  If there is no element of the left
        // we're truly on the boundary
        return !ancestor->pred_;
      }

      // ////////////////////////////////
      //   Same for the right boundary
      // ////////////////////////////////
      // If there's an element to the right we can't be on the boundary
      if (center_->succ_)
        return false;

      const OneDEntityImp<1>* ancestor = center_;

      while (ancestor->level_!=0) {

        // Check if we're the left son of our father
        if (ancestor != ancestor->father_->sons_[1])
          return false;

        ancestor = ancestor->father_;
      }

      // We have reached level 0.  If there is no element of the left
      // we're truly on the boundary
      return !ancestor->succ_;

    }

    //! return true if across the edge an neighbor on this level exists
    bool levelNeighbor () const {
      assert(neighbor_ >= 0 && neighbor_ < 4);

      switch (neighbor_) {
      case 0 :
        return center_->pred_ && center_->pred_->vertex_[1] == center_->vertex_[0];
      case 1 :
        return center_->succ_ && center_->succ_->vertex_[0] == center_->vertex_[1];
      case 2 :
        // true if the leaf neighbor happens to be the level neighbor
        return center_->pred_
               && center_->pred_->vertex_[1] == center_->vertex_[0]
               && center_->pred_->isLeaf();
      default :
        // true if the leaf neighbor happens to be the level neighbor
        return center_->succ_
               && center_->succ_->vertex_[0] == center_->vertex_[1]
               && center_->succ_->isLeaf();
      }

    }

    //! return true if across the edge an neighbor on this level exists
    bool leafNeighbor () const {
      assert(neighbor_ >= 0 && neighbor_ < 4);
      switch (neighbor_) {

      case 0 :
        return center_->isLeaf()
               && center_->pred_
               && center_->pred_->vertex_[1] == center_->vertex_[0]
               && center_->pred_->isLeaf();

      case 1 :
        return center_->isLeaf()
               && center_->succ_
               && center_->succ_->vertex_[0] == center_->vertex_[1]
               && center_->succ_->isLeaf();

      default :
        // neighbor_==2 and neighbor_==3 are leaf neighbors by construction of neighbor_
        return !boundary();
      }

    }

    //! return true if across the edge an neighbor exists
    bool neighbor() const
    {
      return leafNeighbor() || levelNeighbor();
    }

    //! return EntityPointer to the Entity on the inside of this intersection
    //! (that is the Entity where we started this Iterator)
    EntityPointer inside() const
    {
      return OneDGridEntityPointer<0,GridImp>(center_);
    }

    //! return EntityPointer to the Entity on the outside of this intersection
    //! (that is the neighboring Entity)
    EntityPointer outside() const
    {
      return OneDGridEntityPointer<0,GridImp>(target());
    }

    //! ask for level of entity
    int level () const {return center_->level_;}

    //! return information about the Boundary
    int boundaryId () const {
      return 1;
    }

    //! Here returned element is in LOCAL coordinates of the element
    //! where iteration started.
    const LocalGeometry& intersectionSelfLocal () const {
      intersectionSelfLocal_.setPosition( (numberInSelf() == 0) ? 0 : 1 );
      return intersectionSelfLocal_;
    }

    //! intersection of codimension 1 of this neighbor with element where iteration started.
    //! Here returned element is in LOCAL coordinates of neighbor
    const LocalGeometry& intersectionNeighborLocal () const {
      intersectionNeighborLocal_.setPosition( (numberInSelf() == 0) ? 1 : 0 );
      return intersectionNeighborLocal_;
    }

    //! intersection of codimension 1 of this neighbor with element where iteration started.
    //! Here returned element is in GLOBAL coordinates of the element where iteration started.
    const Geometry& intersectionGlobal () const {
      intersectionGlobal_.setToTarget(center_->vertex_[neighbor_%2]);
      return intersectionGlobal_;
    }

    //! local number of codim 1 entity in self where intersection is contained in
    int numberInSelf () const {return neighbor_%2;}

    //! local number of codim 1 entity in neighbor where intersection is contained
    int numberInNeighbor () const {
      // If numberInSelf is 0 then numberInNeighbor is 1 and vice versa
      return 1-(neighbor_%2);
    }

    //! return outer normal
    const FieldVector<typename GridImp::ctype, dimworld>& outerNormal (const FieldVector<typename GridImp::ctype, dim-1>& local) const {
      outerNormal_[0] = ((neighbor_%2)==0) ? -1 : 1;
      return outerNormal_;
    }

    //! Return outer normal scaled with the integration element
    const FieldVector<typename GridImp::ctype, dimworld>& integrationOuterNormal (const FieldVector<typename GridImp::ctype, dim-1>& local) const
    {
      return outerNormal(local);
    }

    //! return unit outer normal
    const FieldVector<typename GridImp::ctype, dimworld>& unitOuterNormal (const FieldVector<typename GridImp::ctype, dim-1>& local) const {
      return outerNormal(local);
    }

  private:
    //**********************************************************
    //  private methods
    //**********************************************************

    OneDEntityImp<1>* center_;

    //! vector storing the outer normal
    mutable FieldVector<typename GridImp::ctype, dimworld> outerNormal_;

    /** \brief Count on which neighbor we are lookin' at

       0,1 are the level neighbors, 2 and 3 are the leaf neighbors,
       if they differ from the level neighbors. */
    int neighbor_;

    /** \brief The geometry that's being returned when intersectionSelfLocal() is called
     */
    mutable OneDMakeableGeometry<0,1,GridImp> intersectionSelfLocal_;

    /** \brief The geometry that's being returned when intersectionNeighborLocal() is called
     */
    mutable OneDMakeableGeometry<0,1,GridImp> intersectionNeighborLocal_;

    //! The geometry that's being returned when intersectionSelfGlobal() is called
    mutable OneDMakeableGeometry<dim-1,dimworld,GridImp> intersectionGlobal_;

  };

}  // namespace Dune

#endif
