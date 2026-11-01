#ifndef TM_H_
#define TM_H_

#ifndef TM_ASSERT
    #include <assert.h>
    #define TM_ASSERT assert
#endif

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <stdio.h>
//#include <stderr>
#include <sys/mman.h>
#include <stdbool.h>

#define OBJECT_MAGIC    UINT64_C(0xC0FFEE1234ABCDEF)
#define CANARY_SIZE     sizeof(uint64_t)
#define CANARY_VALUE    UINT64_C(0xDEADC0DECAFEBABE)
#define REGION_MIN_SIZE 1
#define UNIT            1


// TODO: Struct declarations
struct Object Object;

typedef 
struct Region{
    struct  Region* next;       // Next region
    size_t          mapping_size;// Total allocated by mmap
    size_t          used;        // Region used = 1 / Not used = 0
    size_t          data_offset;// For the first object
    size_t          capacity;   // Usable space for objects
    Object*         free_head;
}Region;
#define REGION_METADATA_SIZE sizeof(Region)

typedef
struct Heap_Relm{
    struct  Region* regions;
    size_t          count;
}Heap_Relm;


typedef
struct Object{
    size_t          size;   // Total payload
    size_t          requested_size; // Usable size requested by the user
    uint64_t        magic;
    unsigned int    used:1;
    struct          Object* prev_free;
    struct          Object* next_free;

    // Newly added
    struct          Region* parent_region;
    // Canary and footer while allocating
}Object;
#define OBJECT_METADATA_SIZE sizeof(Object)

static size_t align_up(size_t n, size_t alignment);

#ifdef TM_IMPLEMENTATION

static 
size_t object_total_size(size_t requested_size)
{
    // Returns padding required
    size_t payload_offset = align_up(sizeof(Object),_Alignof(max_align_t));
    // align_up returns the first argument plus padding
    if (payload_offset == 0) return 0;
    size_t overhead = payload_offset + CANARY_SIZE + sizeof(size_t) /* Footer */;
    if (overhead < OBJECT_METADATA_SIZE) return 0;
    if (requested_size > SIZE_MAX - overhead) return 0;

    size_t total_size = requested_size + overhead;
    return align_up(total_size, _Alignof(max_align_t));
}
static 
void write_canary(Object *obj)
{
    uint64_t canary = CANARY_VALUE;
    size_t payload_offset;
    payload_offset = align_up(sizeof(Object), _Alignof(max_align_t));
    unsigned char *p;
    p = (unsigned char *)obj + payload_offset + obj->requested_size;
    memcpy(p, &canary, sizeof(uint64_t));
}


static 
void write_footer(Object *obj){
    /*
        Move to the end of the object, take a size_t step back, and write obj->size.
    */
    unsigned char *end = (unsigned char *)obj + obj->size;
    memcpy(end - sizeof(size_t), &obj->size, sizeof(size_t));
}

/*
    Initialize the heap.
*/
static Heap_Relm    heap;
static bool         Heap_Relm_initialized;

static
void heap_init(Heap_Relm *h){
    h->regions      =   NULL;
    //h->free_head    =   NULL;
    h->count =   0;
}

static
Heap_Relm *get_heap(void){
    if(!Heap_Relm_initialized){
        heap_init(&heap);
        Heap_Relm_initialized = true;
    }
    return &heap;
}

static 
int redister_region(Region *r){
    Heap_Relm *h = get_heap();
    if(!h){
        return -1;
    }
   r->next      = h->regions;
   h->regions   = r;
   h->count++;

   return 0;
}

/*
    Initial region frame, might be just a single page.
    It covers the region and keeps track of the user allocations and
    free space in the region.
*/
static Region *new_region(size_t capacity);
static 
Region *new_region(size_t capacity){
    size_t alignment = _Alignof(max_align_t);
    size_t data_offset = align_up(sizeof(Region), alignment);
    if (data_offset == 0) return NULL;

    if(capacity > SIZE_MAX - data_offset){
        //TM_ASSERT(capacity < REGION_MIN_SIZE);
        return NULL;
    }
    //size_t size_alloc = sizeof(Region) + (UNIT * capacity);
    size_t size_alloc = data_offset + capacity;
    //void *mmap(void *addr, size_t length, int prot,
    //              int flags, int fd, off_t offset);

    Region *r = mmap(NULL, size_alloc , PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    if (r == MAP_FAILED) {
        //TM_ASSERT(!(r == MAP_FAILED));
        return NULL;
    }
    r->next         = NULL;
    r->mapping_size = size_alloc;
    r->data_offset  = data_offset;
    r->capacity     = capacity;
    r->free_head    = NULL;
    return r;
}


static 
Object* heap_add_region(Region* region){
    /*
        Request to initialize one large object.
        Add metadata and free allocation, then register the region in the heap.
    */
    // size_t total_payload_size = REGION_METADATA_SIZE + PADDING + 
    //                              PAYLOAD + CANARY + FOOTER + ALIGN_PAD
    if(region == NULL) return NULL;
    if(region->capacity < sizeof(Object) + sizeof(size_t))
        return NULL;
    //size_t total_payload_size = (int)(region->size_alloc);
    Object *obj = (Object *)((unsigned char *)region + region->data_offset);
    if(!obj) return NULL;

    obj->size               = region->capacity; // Total payload
    obj->requested_size     = 0;                // As it is a free object
    obj->magic              = OBJECT_MAGIC;
    obj->used               = 0;                // Not used
    obj->prev_free  = NULL;
    obj->next_free  = NULL;
    // New member
    obj->parent_region      =   region;

    write_footer(obj);
    //->freehand removed - added back D2
    //region->free_head = obj;
    if(redister_region(region) != 0){
        return NULL;
    }
    return obj;

}

static 
Object* find_free_object(size_t req_size){
    if(req_size < 1) return NULL;
    Heap_Relm *h = get_heap();
    Object* obj;

   size_t required = object_total_size(req_size);

    Region *r;
    for(r = h->regions; r != NULL ; r = r->next)
    {
        for(obj = r->free_head; obj!=NULL; obj = obj->next_free)
        {
            if(obj->used == 0 && obj->size >=required)
            {
                return obj;
            }
        }
    }
    return NULL;

}

static 
Object* split_object(Object* obj, size_t required_size){
    /*
        obj will be updated for the remainder object.
        newobj is the region requested by the user.
    */

    if(obj == NULL) return NULL;
    if(required_size < 1) return NULL;

    size_t original_size = obj->size;
    size_t t_req_size = object_total_size(required_size);
    if (t_req_size == 0 || original_size < t_req_size)
        return NULL;

    Region *region = obj->parent_region;

    size_t remainder_size = original_size - t_req_size;
    Object *old_prev = obj->prev_free;
    Object *old_next = obj->next_free;
    // Exchange metadata position
    Object *newobj = obj; 
    // Metadata for the new object
    obj = (Object *)((unsigned char *)newobj + t_req_size);

    {
        // Total size: {metadata, padding, object, canary, footer}
        newobj->size            = t_req_size;
        newobj->requested_size  = required_size;
        newobj->magic           = OBJECT_MAGIC;
        newobj->used            = 1;
        newobj->next_free       = NULL;
        newobj->prev_free       = NULL;
        newobj->parent_region   = region;
    }

    // Update the region's free object metadata
    size_t min_free_size = align_up(sizeof(Object) 
                                    + sizeof(size_t),
                                    _Alignof(max_align_t));

    if(remainder_size >= min_free_size){
    // parent_region, requested_size, used, and magic remain constant
        obj->size               = remainder_size;
        obj->requested_size     = 0;
        obj->used               = 0;
        obj->magic              = OBJECT_MAGIC;
        obj->next_free          = old_next;
        obj->prev_free          = old_prev;

        if (old_prev)
            old_prev->next_free = obj;
        else
            region->free_head = obj;

        if (old_next)
            old_next->prev_free = obj;

        obj->parent_region      = region;
        write_footer(newobj);
        write_footer(obj);
    }else{
        // Too small to split
        newobj->size = original_size;
        if (old_prev)
            old_prev->next_free = old_next;
        else
            region->free_head = old_next;

        if (old_next)
            old_next->prev_free = old_prev;

        write_footer(newobj);
    }
    // TODO: Free object head
    return newobj;
}


#endif // TM_IMPLEMENTATION

#endif  // TM_H_
