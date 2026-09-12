# Higher-order collection operations unlocked by capturing function values.
# These are deliberately library code: map/filter/fold do not need compiler magic.

fn list_map<T: Copy, U: Copy>(items: List<T>, transform: fn(T) -> U) -> List<U> {
    let out=list<U>(); for i in 0..list_size(items) { let item: T = list_at(items,i); let mapped: U = transform(item); list_push(out,mapped); } return out;
}
fn list_filter<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> List<T> {
    let out=list<T>(); for i in 0..list_size(items) { let item: T = list_at(items,i); if predicate(item){list_push(out,item);} } return out;
}
fn list_fold<T: Copy, U: Copy>(items: List<T>, initial: U, combine: fn(U,T) -> U) -> U {
    let mut result: U = initial; for i in 0..list_size(items) { let item: T = list_at(items,i); result=combine(result,item); } return result;
}
fn list_any<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> bool { for i in 0..list_size(items) { let item: T=list_at(items,i); if predicate(item){return true;}} return false; }
fn list_all<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> bool { for i in 0..list_size(items) { let item: T=list_at(items,i); if !predicate(item){return false;}} return true; }
fn list_count_if<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> int { let mut count=0; for i in 0..list_size(items) { let item: T=list_at(items,i); if predicate(item){count=count+1;}} return count; }
fn list_find_index<T: Copy>(items: List<T>, predicate: fn(T) -> bool) -> int { let mut index=0; for i in 0..list_size(items) { let item: T=list_at(items,i); if predicate(item){return index;} index=index+1;} return -1; }
fn list_for_each<T: Copy>(items: List<T>, action: fn(T) -> void) -> void { for i in 0..list_size(items) { let item: T=list_at(items,i); action(item);} }
fn list_take<T: Copy>(items: List<T>, count: int) -> List<T> { let out=list<T>(); let mut limit=count; if limit<0{limit=0;} if limit>list_size(items){limit=list_size(items);} for i in 0..limit{list_push(out,list_at(items,i));} return out; }
fn list_drop<T: Copy>(items: List<T>, count: int) -> List<T> { let out=list<T>(); let mut start=count; if start<0{start=0;} if start>list_size(items){start=list_size(items);} for i in start..list_size(items){list_push(out,list_at(items,i));} return out; }

struct Indexed<T> { index: int, value: T }
fn list_enumerate<T: Copy>(items: List<T>) -> List<Indexed<T>> { let out=list<Indexed<T>>(); for i in 0..list_size(items){list_push(out,Indexed<T>(i,list_at(items,i)));} return out; }

struct Pair<T,U> { first:T, second:U }
fn list_zip<T: Copy,U: Copy>(left:List<T>,right:List<U>)->List<Pair<T,U>> { let out=list<Pair<T,U>>(); let mut count=list_size(left); if list_size(right)<count{count=list_size(right);} for i in 0..count{list_push(out,Pair<T,U>(list_at(left,i),list_at(right,i)));} return out; }
