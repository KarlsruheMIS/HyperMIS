/******************************************************************************
 * priority_queue_interface.h 
 *
 * Source of KaHIP -- Karlsruhe High Quality Partitioning.
 *
 *****************************************************************************/

#ifndef PRIORITY_QUEUE_INTERFACE_20ZSYG7R
#define PRIORITY_QUEUE_INTERFACE_20ZSYG7R

#include "definitions.h"

class priority_queue_interface {
        public:
                priority_queue_interface( ) {};
                virtual ~priority_queue_interface() {};

                /* returns the size of the priority queue */
                virtual NodeID size() = 0;
                virtual bool empty()  = 0 ;
               
                virtual void insert(NodeID id, NodeID gain) = 0; 
                
                virtual NodeID minValue()     = 0;
                virtual NodeID minElement() = 0;
                virtual NodeID deleteMin()  = 0;

                virtual void decreaseKey(NodeID node, NodeID newGain) = 0;
                virtual void increaseKey(NodeID node, NodeID newKey)  = 0;

                virtual void changeKey(NodeID element, NodeID newKey) = 0;
                virtual NodeID getKey(NodeID element)  = 0;
                virtual void deleteNode(NodeID node) = 0;
                virtual bool contains(NodeID node)   = 0;
};

typedef priority_queue_interface refinement_pq;

#endif /* end of include guard: PRIORITY_QUEUE_INTERFACE_20ZSYG7R */

