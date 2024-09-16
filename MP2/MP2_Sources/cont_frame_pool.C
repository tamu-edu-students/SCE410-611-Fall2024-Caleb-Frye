/*
 File: ContFramePool.C
 
 Author: Caleb Frye
 Date  : September 10, 2024
 
 */

/*--------------------------------------------------------------------------*/
/* 
 POSSIBLE IMPLEMENTATION
 -----------------------

 The class SimpleFramePool in file "simple_frame_pool.H/C" describes an
 incomplete vanilla implementation of a frame pool that allocates 
 *single* frames at a time. Because it does allocate one frame at a time, 
 it does not guarantee that a sequence of frames is allocated contiguously.
 This can cause problems.
 
 The class ContFramePool has the ability to allocate either single frames,
 or sequences of contiguous frames. This affects how we manage the
 free frames. In SimpleFramePool it is sufficient to maintain the free 
 frames.
 In ContFramePool we need to maintain free *sequences* of frames.
 
 This can be done in many ways, ranging from extensions to bitmaps to 
 free-lists of frames etc.
 
 IMPLEMENTATION:
 
 One simple way to manage sequences of free frames is to add a minor
 extension to the bitmap idea of SimpleFramePool: Instead of maintaining
 whether a frame is FREE or ALLOCATED, which requires one bit per frame, 
 we maintain whether the frame is FREE, or ALLOCATED, or HEAD-OF-SEQUENCE.
 The meaning of FREE is the same as in SimpleFramePool. 
 If a frame is marked as HEAD-OF-SEQUENCE, this means that it is allocated
 and that it is the first such frame in a sequence of frames. Allocated
 frames that are not first in a sequence are marked as ALLOCATED.
 
 NOTE: If we use this scheme to allocate only single frames, then all 
 frames are marked as either FREE or HEAD-OF-SEQUENCE.
 
 NOTE: In SimpleFramePool we needed only one bit to store the state of 
 each frame. Now we need two bits. In a first implementation you can choose
 to use one char per frame. This will allow you to check for a given status
 without having to do bit manipulations. Once you get this to work, 
 revisit the implementation and change it to using two bits. You will get 
 an efficiency penalty if you use one char (i.e., 8 bits) per frame when
 two bits do the trick.
 
 DETAILED IMPLEMENTATION:
 
 How can we use the HEAD-OF-SEQUENCE state to implement a contiguous
 allocator? Let's look a the individual functions:
 
 Constructor: Initialize all frames to FREE, except for any frames that you 
 need for the management of the frame pool, if any.
 
 get_frames(_n_frames): Traverse the "bitmap" of states and look for a 
 sequence of at least _n_frames entries that are FREE. If you find one, 
 mark the first one as HEAD-OF-SEQUENCE and the remaining _n_frames-1 as
 ALLOCATED.

 release_frames(_first_frame_no): Check whether the first frame is marked as
 HEAD-OF-SEQUENCE. If not, something went wrong. If it is, mark it as FREE.
 Traverse the subsequent frames until you reach one that is FREE or 
 HEAD-OF-SEQUENCE. Until then, mark the frames that you traverse as FREE.
 
 mark_inaccessible(_base_frame_no, _n_frames): This is no different than
 get_frames, without having to search for the free sequence. You tell the
 allocator exactly which frame to mark as HEAD-OF-SEQUENCE and how many
 frames after that to mark as ALLOCATED.
 
 needed_info_frames(_n_frames): This depends on how many bits you need 
 to store the state of each frame. If you use a char to represent the state
 of a frame, then you need one info frame for each FRAME_SIZE frames.
 
 A WORD ABOUT RELEASE_FRAMES():
 
 When we releae a frame, we only know its frame number. At the time
 of a frame's release, we don't know necessarily which pool it came
 from. Therefore, the function "release_frame" is static, i.e., 
 not associated with a particular frame pool.
 
 This problem is related to the lack of a so-called "placement delete" in
 C++. For a discussion of this see Stroustrup's FAQ:
 http://www.stroustrup.com/bs_faq2.html#placement-delete
 
 */
/*--------------------------------------------------------------------------*/


/*--------------------------------------------------------------------------*/
/* DEFINES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* INCLUDES */
/*--------------------------------------------------------------------------*/

#include "cont_frame_pool.H"
#include "console.H"
#include "utils.H"
#include "assert.H"

/*--------------------------------------------------------------------------*/
/* DATA STRUCTURES */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* CONSTANTS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* FORWARDS */
/*--------------------------------------------------------------------------*/

/* -- (none) -- */

/*--------------------------------------------------------------------------*/
/* METHODS FOR CLASS   C o n t F r a m e P o o l */
/*--------------------------------------------------------------------------*/

// initialize global frame pool list
ContFramePool* ContFramePool::frame_pools_list = nullptr; 

ContFramePool::ContFramePool(unsigned long _base_frame_no,
                             unsigned long _n_frames,
                             unsigned long _info_frame_no)
{
    //ensure that the bitmap will fit on a single page
    assert(_n_frames <= FRAME_SIZE * 4);

    base_frame_no = _base_frame_no;
    nframes = _n_frames;
    nFreeFrames = _n_frames;
    info_frame_no = _info_frame_no;

    unsigned long n_info_frames = needed_info_frames(nframes);

    unsigned long n_info_frames_needed = needed_info_frames(_n_frames);

    // //This block prints the number of info frames needed for the given pool
    // Console::puts("Need "); 
    // Console::puti(n_info_frames_needed);
    // Console::puts(" frames to store management information\n");

    //set all frames to free initially
    for (int i = base_frame_no; i < base_frame_no + nframes; i++) {
        set_state(i, FrameState::Free);
    }

    /*
        if info frame number = 0, store bitmap in base frame/s. 
        else, it's up to the user (me) to get_frames from an external pool to 
        store management info
    */
    if(info_frame_no == 0) {
        bitmap = (unsigned char *) (base_frame_no * FRAME_SIZE);
        info_frame_no = base_frame_no; 
        get_frames(n_info_frames);
    } else {
        bitmap = (unsigned char *) (info_frame_no * FRAME_SIZE);
        set_state(info_frame_no, FrameState::Used);
    }

    // if no pool exists yet, initialize the list to the current pool
    // otherwise add the current pool to the list
    if (!frame_pools_list) {
        frame_pools_list = this;
    }
    else {
        frame_pools_list->next = this; 
    }
    prev = frame_pools_list;

    // prints initialization information about the pool
    Console::puts("Initialized a Frame Pool with:\n");
    Console::puts("\tframes "); Console::puti(base_frame_no);Console::puts(" to ");Console::puti(base_frame_no + nframes - 1);
    Console::puts("\n");
    Console::puts("\tnframes: "); Console::puti(nframes);Console::puts("\n");
    Console::puts("\tinfo_frame_no: "); Console::puti(info_frame_no);Console::puts("\n\n");
}

unsigned long ContFramePool::get_frames(unsigned int _n_frames)
{
    assert(this->nFreeFrames >= _n_frames);
    assert(_n_frames <= this->nframes);

    // Console::puts("Getting "); Console::puti(_n_frames); Console::puts(" frames\n");

    unsigned int first_frame_of_sequence = 0;
    unsigned long n_contiguous_frames_found = 0;

    for (unsigned long current_frame_no = base_frame_no; current_frame_no < base_frame_no + this->nframes; current_frame_no++) {

        FrameState current_frame_state = get_state(current_frame_no);
        // Console::puts("Current frame state: "); Console::puti((int)current_frame_state); Console::puts("\n");
        // Console::puts("Free state number: "); Console::puti((int)FrameState::Free);Console::puts("\n");

        // found a free frame, now see if there are enough contiguous frames after it.
        // If so, mark the sequence as inaccessible
        if (current_frame_state == FrameState::Free) {
            if (n_contiguous_frames_found == 0) {
                first_frame_of_sequence = current_frame_no;
            }
            n_contiguous_frames_found++;

            if (n_contiguous_frames_found == _n_frames) {
                mark_inaccessible(first_frame_of_sequence, n_contiguous_frames_found);
                return first_frame_of_sequence;
            }
        }
        else {
            n_contiguous_frames_found = 0; 
        }
    }

    Console::puts("Error: unable to find ");
    Console::puti(_n_frames);
    Console::puts(" contiguous free frames\n");
    assert(false);
    return 0;
}

void ContFramePool::mark_inaccessible(unsigned long _base_frame_no,
                                      unsigned long _n_frames)
{
    // ensure the base frame is free
    assert(get_state(_base_frame_no) == FrameState::Free);

    // ensure we aren't trying to mark frames not owned by this frame pool
    assert(_base_frame_no + _n_frames <= this->base_frame_no + this->nframes);

    //iterate from base to end frame, mark each as used
    set_state(_base_frame_no, FrameState::HoS);
	for (int current_frame_no = _base_frame_no + 1; current_frame_no < _base_frame_no + _n_frames; current_frame_no++) {
        assert(get_state(current_frame_no) == FrameState::Free);
        set_state(current_frame_no, FrameState::Used);
	}
	
	nFreeFrames -= _n_frames;

    // //prints information about how many frames were marked inaccessible
    // Console::puts("Marked ");Console::puti(_n_frames);Console::puts(" frames as inaccessible. ");
    // Console::puti(nFreeFrames); Console::puts(" free frames remaining\n");
}

void ContFramePool::release_frames(unsigned long _first_frame_no) {
    ContFramePool* current_pool = frame_pools_list; //start from the first pool in the list
    // traverse the list of pools to find the one that owns this frame
    while (current_pool != nullptr) {
        // check if the frame belongs to the current pool
        if (_first_frame_no >= current_pool->base_frame_no 
                && _first_frame_no < current_pool->base_frame_no + current_pool->nframes) {

            // mark the first frame as Free
            current_pool->set_state(_first_frame_no, FrameState::Free);
            int frames_released = 1; 
            unsigned long current_frame = _first_frame_no + 1;
            current_pool->nFreeFrames++;  // Increment free frames count
            // Console::puts("Released frame number "); Console::puti(_first_frame_no); Console::puts("\n");
            // traverse subsequent frames and mark them as Free until a Free or Head-Of-Sequence frame is encountered
            while (current_frame < current_pool->base_frame_no + current_pool->nframes 
                        && current_pool->get_state(current_frame) == FrameState::Used) {

                // mark the current frame as Free
                current_pool->set_state(current_frame, FrameState::Free);
                // Console::puts("Released frame number "); Console::puti(current_frame); Console::puts("\n");
                current_frame++;
                frames_released++;
                current_pool->nFreeFrames++;  // increment free frames count for each released frame
            }

            // //prints information about the frames that are released 
            // Console::puts("Released "); Console::puti(frames_released); Console::puts(" frames. ");
            // Console::puti(current_pool->nFreeFrames);Console::puts(" free frames remain\n");
            return;  
        }

        // Move to the next pool in the linked list
        current_pool = current_pool->next;
    }

    // if we reach this point, no pool was found that owns the frame
    Console::puts("Error: Frame pool not found for frame ");
    Console::puti(_first_frame_no);
    Console::puts("\n");
    assert(false);  
}


unsigned long ContFramePool::needed_info_frames(unsigned long _n_frames)
{
    return (_n_frames) / (4 * FRAME_SIZE) + (_n_frames % (4 * FRAME_SIZE) > 0 ? 1 : 0);
}

ContFramePool::FrameState ContFramePool::get_state(unsigned long _frame_no) {        
    unsigned int byte_index = _frame_no / 4;  // each byte holds 4 frames
    unsigned int bit_offset = (_frame_no * 2) % 8;  // finds which two bits in the byte we care about

    unsigned char state_bits = (bitmap[byte_index] >> bit_offset) & 0x3; // masks out the two bits (0x3 is 00000011)

    switch (state_bits) {
        case 0x0:
            return FrameState::Free;
        case 0x1:
            return FrameState::Used;
        case 0x2:
            return FrameState::HoS;
        default:
            Console::puts("Error: Invalid frame state\n");
            assert(false);
            return FrameState::Used; // fallback in case of error
    }
}

void ContFramePool::set_state(unsigned long _frame_no, FrameState _state) {
    unsigned int byte_index = _frame_no / 4;  // each byte holds 4 frames
    unsigned int bit_offset = (_frame_no * 2) % 8;  // finds which two bits in the byte we care about

    unsigned char state_bits = 0;
    switch (_state) {
        case FrameState::Free:
            state_bits = 0x0; 
            break;
        case FrameState::Used:
            state_bits = 0x1; 
            break;
        case FrameState::HoS:
            state_bits = 0x2; 
            break;
    }

    bitmap[byte_index] &= ~(0x3 << bit_offset);  // 0x3 = 00000011, shift it to the correct position
    bitmap[byte_index] |= (state_bits << bit_offset); // set the new 2 bit state
}


void ContFramePool::print_pool_info() {
    Console::puts("\nPrinting Pool Info...\n");
    ContFramePool* current_pool = frame_pools_list;
    int i = 1; 
    while (current_pool != nullptr) {
        unsigned long current_pool_needed_info_frames = current_pool->needed_info_frames(current_pool->nframes);
        Console::puts("Pool ["); Console::puti(i); Console::puts("]:\n");
        Console::puts("\t");Console::puts("Frame numbers: "); Console::puti(current_pool->base_frame_no); Console::puts(" to ");
            Console::puti(current_pool->base_frame_no + current_pool->nframes - 1); Console::puts("\n");
        Console::puts("\t");Console::puti(current_pool->nframes); Console::puts(" frames total, ");
            Console::puti(current_pool->nFreeFrames); Console::puts(" frames Free, ");
            Console::puti(current_pool->nframes - current_pool->nFreeFrames); Console::puts(" frames Used.\n");
        Console::puts("\t");Console::puti(current_pool_needed_info_frames); Console::puts(" info frame(s)");
            Console::puts(" at frame number(s): ");
            Console::puti(current_pool->info_frame_no);
        if (current_pool_needed_info_frames > 1) {
            Console::puts("-");
            Console::puti(current_pool->info_frame_no + current_pool_needed_info_frames - 1);
        }
        Console::puts("\n");
        i++;
        current_pool = current_pool->next;
    }
    Console::puts("\n");

}