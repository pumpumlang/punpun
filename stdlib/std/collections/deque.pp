# Amortized O(1) FIFO deque using a List plus a moving head. Front removals do
# not shift the whole list; occasional compaction bounds retained dead slots.
object Deque<T: Copy> {
    public let storage:List<T>; public let head:int;
    public init(){self.storage=list<T>();self.head=0;}
    public fn size()->int{return list_size(self.storage)-self.head;}
    public fn empty()->bool{return self.size()==0;}
    public fn push_back(value:T)->void{list_push(self.storage,value);}
    public fn front()->T{if self.empty(){panic("front of empty deque");}return list_at(self.storage,self.head);}
    public fn back()->T{if self.empty(){panic("back of empty deque");}return list_at(self.storage,list_size(self.storage)-1);}
    public fn pop_front()->T{if self.empty(){panic("pop_front of empty deque");}let value=list_at(self.storage,self.head);self.head=self.head+1;self.compact();return value;}
    public fn pop_back()->T{if self.empty(){panic("pop_back of empty deque");}return list_pop(self.storage);}
    public fn clear()->void{list_clear(self.storage);self.head=0;}
    public fn compact()->void{if self.head<64||self.head*2<list_size(self.storage){return;}let fresh=list<T>();for i in self.head..list_size(self.storage){list_push(fresh,list_at(self.storage,i));}list_clear(self.storage);for value in fresh{list_push(self.storage,value);}self.head=0;}
}
