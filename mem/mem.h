#ifndef TM_H_
#define TM_H_

#ifndef TM_ASSERT
    #include <assert.h>
    #define TM_ASSERT assert
#endif

#include <stdint.h>
#include <stddef.h>

#include <stderr>
#include <sys/mman.h>

#define OBJECT_MAGIC   UINT64_C(0xMAGICBATHIGHFIGH)
#define REGION_MIN_SIZE 1
#define UNIT            1

typedef 
struct Region{
    struct  Region* next;       //Next Region   
    size_t          mapping_size;//total alloc by mmap
    size_t          used;        //region used
    size_t          data_offset;//for the first obj.
    size_t          capacity;   //useable for obj.
    size_t  Object* free_head;
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
    //Canary and Footer while allocation
}Object;

#ifdef TM_IMPLEMENTATION

static 
void write_footer(Object *obj){
    /*
        Move to obj end, take size_t step back and write obj->size
    */
    unsigned char *end = (unsigned char *)obj + obj->size;
    memcpy(end - sizeof(size_t), &obj->size, sizeof(size_t));
}

static 
size_t align_up(size_t n, size_t alignment){
    // [META DATA] [PAD] [OBJ]
    size_t rem = n % alignment;
    if (rem == 0) return n;
    size_t padding = alignment - rem;
    if (n > SIZE_MAX - padding) return 0;

    return n + padding;
}
/*
    Init Fcn for Heap Relm...
*/
static Heap_Relm    heap;
static bool         Heap_Relm_initialized;

static
void heap_init(Heap_Relm *h){
    h->regions      =   NULL;
    h->free_head    =   NULL;
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
    Initial region frame, might be just a single page...
    Covers the region, as the allocation begains it keeps track of
    the user alloc and free to use data segments in Region...

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
        Request to initialize one big a object...
        meta,free alloc, reg region to heap...
    */
    // size_t total_payload_size = REGION_METADATA_SIZE + PADDING + 
    //                              PAYLOAD + CANARY + FOOTER + ALIGN_PAD
    if(region == NULL) return NULL;
    if(region->capacity < sizeof(Object) + sizeof(size_t))
        return NULL;
    size_t total_payload_size = (int)(region->size_alloc);
    Object *obj = (Object *)((unsigned char *)region + region->data_offset);
    if(!obj) return NULL;

    obj->size               = region->capacity; //TOTAL PAYLOAD
    obj->requested_size     = 0;                //As its a free object
    obj->magic              = OBJECT_MAGIC;
    obj->used               = 0;
    obj->prev_free  = NULL;
    obj->next_free  = NULL;
    
    write_footer(obj);
    //->freehand removed - added back D2
    region->free_head = obj;
    if(redister_region(region) != 0){
        return NULL;
    }
    return obj;
    
}
#endif //TM_IMPLEMENTATION

#endif  //TM_H_
