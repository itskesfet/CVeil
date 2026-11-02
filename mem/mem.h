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
#include <stderr>
#include <sys/mman.h>
#include <stdbool.h>



void *tm_alloc(size_t size);
void tm_free(void *ptr);




#ifdef TM_IMPLEMENTATION

#define OBJECT_MAGIC    UINT64_C(0xC0FFEE1234ABCDEF)
#define CANARY_SIZE     sizeof(uint64_t)
#define CANARY_VALUE    UINT64_C(0xDEADC0DECAFEBABE)
#define REGION_MIN_SIZE 1
#define UNIT            1

//TODO struct Decls
typedef struct Object Object;
typedef 
struct Region{
    struct  Region* next;       //Next Region   
    size_t          mapping_size;//total alloc by mmap
    size_t          used;        //region used = 1 / Not used = 0
    size_t          data_offset;//for the first obj.
    size_t          capacity;   //usable for obj.
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
    size_t          size;   //TOTAL PAYLOAD
    size_t          requested_size; //Usable Requested by User
    uint64_t        magic;
    unsigned int    used:1;
    struct          Object* prev_free;
    struct          Object* next_free;
    
    //newly added
    struct          Region* parent_region;
    //Canary and Footer while allocation
}Object;
#define OBJECT_METADATA_SIZE sizeof(Object)

typedef enum {
    TM_OK,
    TM_ERROR,
    TM_OUT_OF_MEMORY
    TM_OUT_OF_MEMORY
} TM_Status;

static size_t   align_up(size_t n, size_t alignment);
static size_t   object_total_size(size_t requested_size);
static void     write_canary(Object *obj);
static void     write_footer(Object *obj);
static bool     check_canary(Object *obj);
static void     heap_init(Heap *heap);
static Heap     *get_heap(void);
static int      register_region(Region *region);
static Region   *region_create(size_t capacity);
static Object   *heap_add_region(Region *region);
static Object   *find_free_object(size_t size);
static Object   *split_object(Object *obj, size_t size);

static 
size_t object_total_size(size_t requested_size)
{
    //returns padding required
    size_t payload_offset = align_up(sizeof(Object),_Alignof(max_align_t));
    //align_up returns first arg + padding
    if (payload_offset == 0) return 0;
    size_t overhead = payload_offset + CANARY_SIZE + sizeof(size_t) /*FOOTER*/;
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
void write_footer(Object *obj)
{
    //    Move to obj end, take size_t step back and write obj->size
    unsigned char *end = (unsigned char *)obj + obj->size;
    memcpy(end - sizeof(size_t), &obj->size, sizeof(size_t));
}

static 
size_t align_up(size_t n, size_t alignment)
{
    // [META DATA] [PAD] [OBJ]
    size_t rem = n % alignment;
    if (rem == 0) return n;
    size_t padding = alignment - rem;
    if (n > SIZE_MAX - padding) return 0;
    return n + padding;
}

static Heap_Relm    heap;
static bool         Heap_Relm_initialized;
static
void heap_init(Heap_Relm *h)
{
    h->regions      =   NULL;
    //h->free_head    =   NULL;
    h->count =   0;
}

static
Heap_Relm *get_heap(void)
{
    if(!Heap_Relm_initialized){
        heap_init(&heap);
        Heap_Relm_initialized = true;
    }
    return &heap;
}

static 
int redister_region(Region *r)
{
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
    Initial region frame, might be just a single page...
    Covers the region, as the allocation begins it keeps track of
    the user alloc and free to use data segments in Region...

*/
static Region *new_region(size_t capacity);
static 
Region *new_region(size_t capacity)
{
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
    r->used = 0;
    r->capacity     = capacity;
    r->free_head    = NULL;
    return r;
}


static 
Object* heap_add_region(Region* region)
{
    /*
        Request to initialize one big a object...
        meta,free alloc, reg region to heap...
    */
    // size_t total_payload_size = REGION_METADATA_SIZE + PADDING + 
    //                              PAYLOAD + CANARY + FOOTER + ALIGN_PAD
    if(region == NULL) return NULL;
    if(region->capacity < sizeof(Object) + sizeof(size_t))
        return NULL;
    //size_t total_payload_size = (int)(region->size_alloc);
    Object *obj = (Object *)((unsigned char *)region + region->data_offset);
    if(!obj) return NULL;

    obj->size               = region->capacity; //TOTAL PAYLOAD
    obj->requested_size     = 0;                //As its a free object
    obj->magic              = OBJECT_MAGIC;
    obj->used               = 0;                //Not used
    obj->prev_free  = NULL;
    obj->next_free  = NULL;
    //NEW member
    region->free_head = obj;
    obj->parent_region      =   region;

    write_footer(obj);
    //->freehand removed - added back D2
    //region->free_head = obj;
    if(redister_region(region) != 0){
        munmap(region, region->mapping_size);
        return NULL;
    }
    return obj;
    
}

static 
Object* find_free_object(size_t req_size)
{
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
Object* split_object(Object* obj, size_t required_size)
{
    /*
        obj will be updated for reminder obj
        newobj is the requested region by user
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
    //exhange metadata position
    //TODO Push it after reminder size check
    Object *newobj = obj; 
    //metadata post new obj
    obj = (Object *)((unsigned char *)newobj + t_req_size);
    
    {
        //total size {meta, padding, obj , canery, footer}
        newobj->size            = t_req_size;
        newobj->requested_size  = required_size;
        newobj->magic           = OBJECT_MAGIC;
        newobj->used            = 1;
        newobj->next_free       = NULL;
        newobj->prev_free       = NULL;
        newobj->parent_region   = region;
    }
    
    //Update region free object metadata
    size_t min_free_size = align_up(sizeof(Object) 
                                    + sizeof(size_t),
                                    _Alignof(max_align_t));
    
    if(remainder_size >= min_free_size){
    //parent_region,requested_size,used,magic Remains constant
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
        //TO small for split
        newobj->size = original_size;
        if (old_prev)
            old_prev->next_free = old_next;
        else
            region->free_head = old_next;

        if (old_next)
            old_next->prev_free = old_prev;

        write_footer(newobj);
    }
    //TODO free obj head 
    return newobj;
}

//D4
static 
bool check_canary(Object *obj)
{
    size_t payload_offset = align_up(sizeof(Object),
                                     _Alignof(max_align_t));
    if (payload_offset == 0)
        return false;
    size_t remaining = obj->size - payload_offset;
    if (obj->size < payload_offset || remaining < CANARY_SIZE + sizeof(size_t))
        return false;
    if (obj->requested_size >
        remaining - CANARY_SIZE - sizeof(size_t))
        return false;

    uint64_t canary;
    memcpy(&canary,
           (unsigned char *)obj + payload_offset + obj->requested_size,
           sizeof(canary));

    return canary == CANARY_VALUE;
}


#define REGION_DEFAULT_CAPACITY (64 * 1024)

void *tm_alloc(size_t size)
{
    if (size == 0) return NULL;
    size_t required_size = object_total_size(size);
    if (required_size == 0) return NULL;

    Object *obj = find_free_object(size);

    /*
       No suitable Object found
       Create and register a new regon.
     */
    if (obj == NULL) {
            size_t capacity = required_size;

            if (capacity < REGION_DEFAULT_CAPACITY)
            {
                capacity = REGION_DEFAULT_CAPACITY;
            }

            Region *region = new_region(capacity);

            if (region == NULL) return NULL;

            if (!heap_add_region(region)) return NULL;

            obj = region->free_head;

            if (obj == NULL || obj->size < required_size) return NULL;
    }

    Object *allocated = split_object(obj, size);

    if (allocated == NULL) return NULL;

    write_canary(allocated);
    size_t payload_offset = align_up(sizeof(Object),
                                     _Alignof(max_align_t));
    return (unsigned char *)allocated + payload_offset;
}

void tm_free(void *ptr)
{
    //TODO pointer space check
    if (ptr == NULL) return;

    size_t payload_offset = align_up(sizeof(Object),
                                     _Alignof(max_align_t));

    if (payload_offset == 0) return;

    Object *obj = (Object *)((unsigned char *)ptr - payload_offset);

    if (obj->magic != OBJECT_MAGIC || !obj->used) {
        TM_ASSERT(obj->magic == OBJECT_MAGIC && obj->used);
        return;
    }

    bool valid = check_canary(obj);    
    //TODO check it yo
    TM_ASSERT(valid);
    if (!valid) return;

    Region *region = obj->parent_region;

    if (region == NULL)
        return;

    obj->used = 0;
    obj->requested_size = 0;

    obj->prev_free = NULL;
    obj->next_free = region->free_head;

    if (region->free_head != NULL)
        region->free_head->prev_free = obj;

    region->free_head = obj;
    write_footer(obj);
}
#endif //TM_IMPLEMENTATION

#endif  //TM_H_
