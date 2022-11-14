#include "list.h"
#include "../debug.h"

/* Our doubly linked lists have two header elements: the "head"
   just before the first element and the "tail" just after the
   last element.  The `prev' link of the front header is null, as
   is the `next' link of the back header.  Their other two links
   point toward each other via the interior elements of the list.

   An empty list looks like this:

	   +------+     +------+
   <---| head |<--->| tail |--->
	   +------+     +------+

   A list with two elements in it looks like this:

	   +------+     +-------+     +-------+     +------+
   <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
	   +------+     +-------+     +-------+     +------+

   The symmetry of this arrangement eliminates lots of special
   cases in list processing.  For example, take a look at
   list_remove(): it takes only two pointer assignments and no
   conditionals.  That's a lot simpler than the code would be
   without header elements.

   (Because only one of the pointers in each header element is used,
   we could in fact combine them into a single header element
   without sacrificing this simplicity.  But using two separate
   elements allows us to do a little bit of checking on some
   operations, which can be valuable.) */

static bool is_sorted(struct list_elem *a, struct list_elem *b,
					  list_less_func *less, void *aux) UNUSED;

/* Returns true if ELEM is a head, false otherwise. */
static inline bool
is_head(struct list_elem *elem)
{
	return elem != NULL && elem->prev == NULL && elem->next != NULL; // 엘리먼트가 널이 아님, 프리브도 널이 아님, 넥스트도 널이 아님
}

/* Returns true if ELEM is an interior element,
   false otherwise. */
static inline bool
is_interior(struct list_elem *elem)
{
	return elem != NULL && elem->prev != NULL && elem->next != NULL; // 엘리먼트가 널이 아님, 프리브도 널이 아님, 넥스트도 널이 아님
}

/* Returns true if ELEM is a tail, false otherwise. */
static inline bool
is_tail(struct list_elem *elem)
{
	return elem != NULL && elem->prev != NULL && elem->next == NULL; // 엘리먼트가 널이 아님, 프리브가 널이 아님, 넥스트가 널임
}

/* Initializes LIST as an empty list. */
void list_init(struct list *list) // 아무것도 없는 리스트 이닛
{
	ASSERT(list != NULL);		   // 리스트가 널이 아닌 것이 맞는지 확인
	list->head.prev = NULL;		   // 헤드의 프리브를 널로 설정
	list->head.next = &list->tail; // 헤드의 넥스트를 테일로 설정
	list->tail.prev = &list->head; // 테일의 프리브를 헤드로 설정
	list->tail.next = NULL;		   // 테일의 넥스트를 널로 설정
}

/* Returns the beginning of LIST.  */
struct list_elem *
list_begin(struct list *list) // 리스트의 처음을 리턴
{
	ASSERT(list != NULL);	// 리스트는 널이 아니여야됨
	return list->head.next; // 헤드의 다음을 리턴
}

/* Returns the element after ELEM in its list.  If ELEM is the
   last element in its list, returns the list tail.  Results are
   undefined if ELEM is itself a list tail. */
struct list_elem *
list_next(struct list_elem *elem) // 다음 엘리먼트를 리턴
{
	ASSERT(is_head(elem) || is_interior(elem)); // 엘리먼트가 헤드이거나, 중간에 있어야됨
	return elem->next;							// 엘리먼트의 넥스트로 포인터 재지정
}

/* Returns LIST's tail.

   list_end() is often used in iterating through a list from
   front to back.  See the big comment at the top of list.h for
   an example. */
struct list_elem *
list_end(struct list *list) // 리스트의 테일 리턴
{
	ASSERT(list != NULL); // 리스트가 널이 아니어야됨
	return &list->tail;	  // 리스트의 테일리턴
}

/* Returns the LIST's reverse beginning, for iterating through
   LIST in reverse order, from back to front. */
struct list_elem *
list_rbegin(struct list *list) // 리스트의 테일에서 바로 앞 리턴 / 맨 뒤에서 첫번째
{
	ASSERT(list != NULL);	// 리스트는 널이 아니여야됨
	return list->tail.prev; // 리스트의 테일에서 바로 앞 리턴
}

/* Returns the element before ELEM in its list.  If ELEM is the
   first element in its list, returns the list head.  Results are
   undefined if ELEM is itself a list head. */
struct list_elem *
list_prev(struct list_elem *elem) //엘리먼트의 바로 앞 리턴
{
	ASSERT(is_interior(elem) || is_tail(elem)); // 엘리먼트가 중간이거나 테일이어야됨
	return elem->prev;							// 엘리먼트의 프리브 리턴
}

/* Returns LIST's head.

   list_rend() is often used in iterating through a list in
   reverse order, from back to front.  Here's typical usage,
   following the example from the top of list.h:

   for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
   e = list_prev (e))
   {
   struct foo *f = list_entry (e, struct foo, elem);
   ...do something with f...
   }
   */
struct list_elem *
list_rend(struct list *list) // 리스트의 헤드 리턴
{
	ASSERT(list != NULL); // 리스트는 널이 아니어야됨
	return &list->head;	  // 리스트의 헤드 리턴
}

/* Return's LIST's head.

   list_head() can be used for an alternate style of iterating
   through a list, e.g.:

   e = list_head (&list);
   while ((e = list_next (e)) != list_end (&list))
   {
   ...
   }
   */
struct list_elem *
list_head(struct list *list) // 리스트의 헤드 리턴
{
	ASSERT(list != NULL); // 리스트는 널이 아니어야됨
	return &list->head;	  // 리스트의 헤드 리턴
}

/* Return's LIST's tail. */
struct list_elem *
list_tail(struct list *list) //리스트의 테일 리턴
{
	ASSERT(list != NULL); // 리스트는 널이 아니어야 됨
	return &list->tail;	  // 리스트의 테일 리턴
}

/* Inserts ELEM just before BEFORE, which may be either an
   interior element or a tail.  The latter case is equivalent to
   list_push_back(). */
void list_insert(struct list_elem *before, struct list_elem *elem) // 비포의 이전에 삽입하는 함수
{
	ASSERT(is_interior(before) || is_tail(before)); // 비포가 중간에 있거나 꼬리에 있어야됨
	ASSERT(elem != NULL);							// 엘리먼트가 널이면 안 됨

	elem->prev = before->prev; // 엘리먼트의 프리브가 비포의 프리브가 되어야됨
	elem->next = before;	   // 엘리먼트의 넥스트가 비포의 넥스트여야 됨
	before->prev->next = elem; // 비포의 프리브의 넥스트가 엘리먼트어야됨
	before->prev = elem;	   // 비포의 프리브가 엘리먼트여야됨
}

/* Removes elements FIRST though LAST (exclusive) from their
   current list, then inserts them just before BEFORE, which may
   be either an interior element or a tail. */
void list_splice(struct list_elem *before,
				 struct list_elem *first, struct list_elem *last) // 퍼스트부터 라스트까지를 잘라서 비포의 이전에 삽입하는 것
{
	ASSERT(is_interior(before) || is_tail(before)); //비포가 중간에 있거나, 꼬리에 있어야됨
	if (first == last)								// 퍼스트와 라스트가 같다면 그냥 리턴
		return;
	last = list_prev(last); // 아니라면 라스트는 라스트의 이전

	ASSERT(is_interior(first)); //퍼스트가 중간에 있는지 체크
	ASSERT(is_interior(last));	// 라스트가 중간에 있는지 체크

	/* Cleanly remove FIRST...LAST from its current list. */
	first->prev->next = last->next; //퍼스트의 이전의 다음이, 라스트의 다음
	last->next->prev = first->prev; // 라스트의 다음의 이전이, 퍼스트의 이전

	/* Splice FIRST...LAST into new list. */
	first->prev = before->prev; // 퍼스트의 이전이 비포의 이전
	last->next = before;		// 라스트의 다음이 비포
	before->prev->next = first; // 비포의 이전의 다음이 퍼스트
	before->prev = last;		// 비포의 이전이 라스트
}

/* Inserts ELEM at the beginning of LIST, so that it becomes the
   front in LIST. */
void list_push_front(struct list *list, struct list_elem *elem) // 리스트의 맨 앞에 삽입
{
	list_insert(list_begin(list), elem); // 리스트의 맨 앞 전에 삽입
}

/* Inserts ELEM at the end of LIST, so that it becomes the
   back in LIST. */
void list_push_back(struct list *list, struct list_elem *elem)
{
	list_insert(list_end(list), elem); // 리스트의 테일 전에 삽입
}

/* Removes ELEM from its list and returns the element that
   followed it.  Undefined behavior if ELEM is not in a list.

   It's not safe to treat ELEM as an element in a list after
   removing it.  In particular, using list_next() or list_prev()
   on ELEM after removal yields undefined behavior.  This means
   that a naive loop to remove the elements in a list will fail:

 ** DON'T DO THIS **
 for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 {
 ...do something with e...
 list_remove (e);
 }
 ** DON'T DO THIS **

 Here is one correct way to iterate and remove elements from a
list:

for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
{
...do something with e...
}

If you need to free() elements of the list then you need to be
more conservative.  Here's an alternate strategy that works
even in that case:

while (!list_empty (&list))
{
struct list_elem *e = list_pop_front (&list);
...do something with e...
}
*/
struct list_elem *
list_remove(struct list_elem *elem) // 중간에 있는 엘리먼트 삭제
{
	ASSERT(is_interior(elem));	   // 엘리먼트가 중간에 있는지 확인
	elem->prev->next = elem->next; // 엘리먼트의 이전에 다음이 엘리먼트의 다음
	elem->next->prev = elem->prev; // 엘리먼트의 다음의 이전이 엘리먼트의 이전
	return elem->next;			   // 엘리먼트 다음 리턴
}

/* Removes the front element from LIST and returns it.
   Undefined behavior if LIST is empty before removal. */
struct list_elem *
list_pop_front(struct list *list) // 리스트의 맨 앞 엘리먼트를 리무브하고 그 엘리먼트를 리턴
{
	struct list_elem *front = list_front(list); // 리스트의 맨 앞 엘리먼트
	list_remove(front);							// 맨 앞 엘리먼트 제거
	return front;								// 제거된 엘리먼트 반환
}

/* Removes the back element from LIST and returns it.
   Undefined behavior if LIST is empty before removal. */
struct list_elem *
list_pop_back(struct list *list) // 리스트의 맨 뒤 엘리먼트를 리무브 하고, 그 엘리먼트 리턴
{
	struct list_elem *back = list_back(list); // 리스트의 맨 뒤 엘리먼트
	list_remove(back);						  // 해당 엘리먼트 제거
	return back;							  // 제거된 엘리번트 리턴
}

/* Returns the front element in LIST.
   Undefined behavior if LIST is empty. */
struct list_elem *
list_front(struct list *list) // 리스트의 맨 앞 엘리먼트를 리턴
{
	ASSERT(!list_empty(list)); // 리스트가 비어있지 않아야 됨
	return list->head.next;	   //리스트의 헤드의 다음 출력 (맨 앞)
}

/* Returns the back element in LIST.
   Undefined behavior if LIST is empty. */
struct list_elem *
list_back(struct list *list) // 리스트의 맨 뒤 엘리먼트를 리턴
{
	ASSERT(!list_empty(list)); // 리스트가 비어있지 않아야 됨
	return list->tail.prev; // 리스트의 테일의 앞 (맨 뒤)
}

/* Returns the number of elements in LIST.
   Runs in O(n) in the number of elements. */
size_t
list_size(struct list *list) //리스트에 포함된 엘리먼트가 몇개인지 리턴
{
	struct list_elem *e;
	size_t cnt = 0;

	for (e = list_begin(list); e != list_end(list); e = list_next(e))
		cnt++;
	return cnt;
}

/* Returns true if LIST is empty, false otherwise. */
bool list_empty(struct list *list) // 리스트가 비어있는지 확인하는 함수 ( t or f )
{
	return list_begin(list) == list_end(list);
}

/* Swaps the `struct list_elem *'s that A and B point to. */
static void
swap(struct list_elem **a, struct list_elem **b) // 리스트의 엘리먼트 a,b를 서로 스왑
{
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* Reverses the order of LIST. */
void list_reverse(struct list *list)
{
	if (!list_empty(list)) // 리스트가 비어있지 않다면
	{
		struct list_elem *e;

		for (e = list_begin(list); e != list_end(list); e = e->prev)
			swap(&e->prev, &e->next);
		swap(&list->head.next, &list->tail.prev);
		swap(&list->head.next->prev, &list->tail.prev->next);
	}
}

/* Returns true only if the list elements A through B (exclusive)
   are in order according to LESS given auxiliary data AUX. */
static bool
is_sorted(struct list_elem *a, struct list_elem *b,
		  list_less_func *less, void *aux)
{
	if (a != b)
		while ((a = list_next(a)) != b)
			if (less(a, list_prev(a), aux))
				return false;
	return true;
}

/* Finds a run, starting at A and ending not after B, of list
   elements that are in nondecreasing order according to LESS
   given auxiliary data AUX.  Returns the (exclusive) end of the
   run.
   A through B (exclusive) must form a non-empty range. */
static struct list_elem *
find_end_of_run(struct list_elem *a, struct list_elem *b,
				list_less_func *less, void *aux)
{
	ASSERT(a != NULL);	  // a는 null이 아니어야 됨
	ASSERT(b != NULL);	  // b는 null이 아니어야 됨
	ASSERT(less != NULL); // less 함수는 null이 아니어야 됨
	ASSERT(a != b);		  // a는 b가 아니어야 됨

	do
	{
		a = list_next(a);
	} while (a != b && !less(a, list_prev(a), aux));
	return a;
}

/* Merges A0 through A1B0 (exclusive) with A1B0 through B1
   (exclusive) to form a combined range also ending at B1
   (exclusive).  Both input ranges must be nonempty and sorted in
   nondecreasing order according to LESS given auxiliary data
   AUX.  The output range will be sorted the same way. */
static void
inplace_merge(struct list_elem *a0, struct list_elem *a1b0,
			  struct list_elem *b1,
			  list_less_func *less, void *aux)
{
	ASSERT(a0 != NULL);
	ASSERT(a1b0 != NULL);
	ASSERT(b1 != NULL);
	ASSERT(less != NULL);
	ASSERT(is_sorted(a0, a1b0, less, aux));
	ASSERT(is_sorted(a1b0, b1, less, aux));

	while (a0 != a1b0 && a1b0 != b1)
		if (!less(a1b0, a0, aux))
			a0 = list_next(a0);
		else
		{
			a1b0 = list_next(a1b0);
			list_splice(a0, list_prev(a1b0), a1b0);
		}
}

/* Sorts LIST according to LESS given auxiliary data AUX, using a
   natural iterative merge sort that runs in O(n lg n) time and
   O(1) space in the number of elements in LIST. */
void list_sort(struct list *list, list_less_func *less, void *aux)
{
	size_t output_run_cnt; /* Number of runs output in current pass. */

	ASSERT(list != NULL); // 리스트가 널이 아니여야됨
	ASSERT(less != NULL); // 레스함수가 널이 아니여야됨

	/* Pass over the list repeatedly, merging adjacent runs of
	   nondecreasing elements, until only one run is left. */
	do
	{
		struct list_elem *a0;	/* Start of first run. */
		struct list_elem *a1b0; /* End of first run, start of second. */
		struct list_elem *b1;	/* End of second run. */

		output_run_cnt = 0;
		for (a0 = list_begin(list); a0 != list_end(list); a0 = b1)
		{
			/* Each iteration produces one output run. */
			output_run_cnt++;

			/* Locate two adjacent runs of nondecreasing elements
			   A0...A1B0 and A1B0...B1. */
			a1b0 = find_end_of_run(a0, list_end(list), less, aux);
			if (a1b0 == list_end(list))
				break;
			b1 = find_end_of_run(a1b0, list_end(list), less, aux);

			/* Merge the runs. */
			inplace_merge(a0, a1b0, b1, less, aux);
		}
	} while (output_run_cnt > 1);

	ASSERT(is_sorted(list_begin(list), list_end(list), less, aux));
}

/* Inserts ELEM in the proper position in LIST, which must be
   sorted according to LESS given auxiliary data AUX.
   Runs in O(n) average case in the number of elements in LIST. */
void list_insert_ordered(struct list *list, struct list_elem *elem,
						 list_less_func *less, void *aux) // 리스트를 순회하다가 넣으려는 엘리먼트보다 큰 엘리먼트 앞에 삽입
{
	struct list_elem *e;

	ASSERT(list != NULL); // 리스트가 널이 아닌지 확인
	ASSERT(elem != NULL); // elem 이 널이 아닌지 확인
	ASSERT(less != NULL); // 레스함수가 널이 아닌지 확인

	for (e = list_begin(list); e != list_end(list); e = list_next(e)) // 리스트를 순회하면서
		if (less(elem, e, aux))										  // 넣으려는 엘리먼트가 e보다 작으면 중지
			break;
	return list_insert(e, elem); // e 앞에다가 삽입해줌
}

/* Iterates through LIST and removes all but the first in each
   set of adjacent elements that are equal according to LESS
   given auxiliary data AUX.  If DUPLICATES is non-null, then the
   elements from LIST are appended to DUPLICATES. */
void list_unique(struct list *list, struct list *duplicates,
				 list_less_func *less, void *aux) // 리스트를 순회하면서 인접한 엘리먼트 중에 같은 엘리먼트가 있다면, 리스트에서 빼서 듀플리케이트리스트로 넣어줌
{
	struct list_elem *elem, *next;

	ASSERT(list != NULL); // 리스트가 널이 아닌지 확인
	ASSERT(less != NULL); // less 함수가 널이 아닌지 확인
	if (list_empty(list)) // list가 비어있으면 리턴
		return;

	elem = list_begin(list);								  // 리스트의 처음 엘리먼트를 선언
	while ((next = list_next(elem)) != list_end(list))		  // 다음 엘리먼트가 테일이 아닐때까지
		if (!less(elem, next, aux) && !less(next, elem, aux)) //만약에 엘리먼트가 넥스트와 같다면 (엘리먼트가 넥스트보다 작지 않고, 넥스트가 엘리먼트보다 작지 않다면)
		{
			list_remove(next);					  // 넥스트 리무브
			if (duplicates != NULL)				  // 복제 리스트가 널이 아니라면
				list_push_back(duplicates, next); // 복제 리스트에 넥스트를 넣음
		}
		else
			elem = next; //다음 타자로 순회
}

/* Returns the element in LIST with the largest value according
   to LESS given auxiliary data AUX.  If there is more than one
   maximum, returns the one that appears earlier in the list.  If
   the list is empty, returns its tail. */
struct list_elem *
list_max(struct list *list, list_less_func *less, void *aux) // list에서 max값의 element 리턴
{
	struct list_elem *max = list_begin(list); // 리스트의 처음엘리먼트를 max값으로 선언
	if (max != list_end(list))				  // 처음 엘리먼트가 마지막이 아니라면 (즉, 엘리먼트가 하나라면)
	{
		struct list_elem *e;

		for (e = list_next(max); e != list_end(list); e = list_next(e)) // 마지막까지 순회하면서
			if (less(max, e, aux))										// max 값이 e보다 작다면
				max = e;												// max 값으로 업데이트
	}
	return max; // 리턴 max
}

/* Returns the element in LIST with the smallest value according
   to LESS given auxiliary data AUX.  If there is more than one
   minimum, returns the one that appears earlier in the list.  If
   the list is empty, returns its tail. */
struct list_elem *
list_min(struct list *list, list_less_func *less, void *aux) // list에서 min값의 element 리턴
{
	struct list_elem *min = list_begin(list); // 리스트의 처음엘리먼트를 min값으로 선언
	if (min != list_end(list))				  // 처음 엘리먼트가 마지막이 아니라면 (즉, 엘리먼트가 하나라면)
	{
		struct list_elem *e;

		for (e = list_next(min); e != list_end(list); e = list_next(e)) // 마지막까지 순회하면서
			if (less(e, min, aux))										// e가 min 보다 작은지 확인
				min = e;												// e가 min보다 작으면 업데이트
	}
	return min; // min element 출력
}
