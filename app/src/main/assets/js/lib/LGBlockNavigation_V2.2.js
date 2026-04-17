/*
 * LG Navigation V2.2.2
 * issues with Disabled status
 * LG Navigation V2.2.1
 * MRCU, RCU support
 * Idle/Focused/Selected/Disabled/Hidden 
 * styles as  part of style list.
 * scroll Blocks
 * isMRCUOn added
 *
 * var yourBlockList = new BlockListClass();
 * var yourButton = cpPlay = document.getElementById([yourButtonId]);
 * yourBlockList.addBlock([blockId],[direction], Array(yourButton,...[other buttons])); //[direction] 1 is horizontal/0 is vertical
 * yourBlockList.setFocusToElement([blockId],yourButton);//initial focus
 * yourBlockList.getBlockById([blockId]).setNextBlockLRUD(0,[ blockId_2],"blockId_3",0); //relation from block to block
 * yourBlockList.getBlockById([blockId]).setNextBlockWayLRUD(0,TO_FIRST,TO_SAME,0); //TO_FIRST/TO_LAST/TO_SAME/0
 * yourBlockList.initialMouseOverForAll();
 * 
 * 
 *   
 *   
 */
var TO_FIRST            = 	0;
var TO_LAST		=	1;
var TO_SAME		=	2;	

var CN_IDLE		=	"idle";
var CN_FOCUS		=	"focused";
var CN_SELECTED		=	"selected";
var CN_DISABLED		=	"setdisabled";

var focusClass = function(blockId,elementId) {
	this.blockId = blockId;
	this.elementId = elementId;
	this.isSameAs = function(checkBlockId,checkElementId){
		if(~this.blockId.indexOf(checkBlockId) && this.elementId == checkElementId)
			return true;
		else
			return false;
	};
};
function setClassName(object,newClassName){		
	var tmpClassName = object.className.split(' ');
	for(var i=0; i<tmpClassName.length; i++) {
		    if (~tmpClassName[i].indexOf(CN_IDLE)|| 
		    		~tmpClassName[i].indexOf(CN_FOCUS)||
		    		~tmpClassName[i].indexOf(CN_SELECTED)||
		    		~tmpClassName[i].indexOf(CN_DISABLED)) 
		    	{
		    		tmpClassName.splice(i,1);
		    		i--;
		    	}	    		    	
	}
	tmpClassName.push(newClassName);
	object.className = tmpClassName.join(' ');
};
function isClassNameExist(object,testClassName){		
	var tmpClassName = object.className.split(' ');
	for(var i=0; i<tmpClassName.length; i++) {
		    if (~tmpClassName[i].indexOf(testClassName)){ 
		    	return true;			    
		    }
	}
	return false;	
};
function isMRCUOn()
{
    if(window.NetCastGetMouseOnOff) {
        if(window.NetCastGetMouseOnOff()=='on')
            return true;
    }
    else{
       // return true;// for chrome checking
    }
    return false;
}
/****************************************************************************************************************************/
//create one BlockList for each page
var BlockListClass = function(){
	this.blockArray=[];
	this.addBlock = function(id,direction,list){	
		this.blockArray.push(new BlockClass(id,direction,list,this));	
        document.body.onkeydown = this.processKeyDown;    
        //window.onmousemove = this.blockListOnMouseMove;
        window.onmouseon = this.blockListOnMouseOn;
        window.onmouseoff = this.blockListOnMouseOff;
	};
    this.addScrollBlock = function(id,direction,maxVisible,list){    
        this.addBlock(id,direction,list);
        this.getBlockById(id).setAsScrollBlock(maxVisible);                                         
    };
	this.getBlockById = function(id){				
		for(var i = 0; i < this.blockArray.length ;i++){
			if(~this.blockArray[i].blockId.indexOf(id)) {							
				return this.blockArray[i];
                        }
		}		
	};	
	this.getBlockIndexById = function(id){
		for(var i = 0; i < this.blockArray.length ;i++){
			if(~this.blockArray[i].blockId.indexOf(id))				
				return i;			
		}
		return null;
	};
	this.setNextBlockListLRUD = function(id,L,R,U,D){
		this.getBlockById(id).setNextBlockLRUD(L,R,U,D);			
	};	
	this.getActiveBlock = function(){
		for(var i = 0; i < this.blockArray.length;i++){
			if(this.blockArray[i].isBlockActive)		
				return this.blockArray[i];			
		}
		return this.blockArray[0];		
	};
	this.releaseBlockListFocus = function(){	        
		this.getActiveBlock().releaseBlockFocus();
	};
	this.activateBlockListFocus = function(){            
		this.getActiveBlock().activateBlockFocus();
	};
	this.setInitialFocus = function(){
		this.blockArray[0].setActiveBlock();
		this.blockArray[0].currentFocus = 0;
	};
	this.setFocusToElement = function(blockId,elementId){		
	//	this.getActiveBlock().isBlockActive = false;
		this.releaseBlockListFocus();
		var block = this.getBlockById(blockId);		
		block.setActiveBlock();		
		block.setCurrentFocus(elementId);
		if(!isMRCUOn()){
			block.activateBlockFocus();
		}
	};
	this.setDisabledElement = function(blockId,elementId){				
			this.getBlockById(blockId).setDisabledElement(elementId);			
	};
	this.setEnabledElement = function(blockId,elementId){				
		this.getBlockById(blockId).setEnabledElement(elementId);			
	};
	this.setSelectedElement = function(blockId,elementId){				
		this.getBlockById(blockId).setSelectedElement(elementId);			
	};
	this.setFocusOnNextElement = function (direction){			
		var activeBlock = this.getActiveBlock();		
		var currentFocus = new focusClass(activeBlock.blockId,activeBlock.currentFocus);		
		var nextFocus = this.findNextElement(direction,currentFocus);
		if (nextFocus){		
			if(!nextFocus.isSameAs(currentFocus.blockId,currentFocus.elementId)){				
				var currentBlock = this.getBlockById(currentFocus.blockId);
				currentBlock.releaseBlockFocus();
				
				var nextBlock = this.getBlockById(nextFocus.blockId);
				nextBlock.currentFocus = nextFocus.elementId;
				nextBlock.setActiveBlock();
				nextBlock.activateBlockFocus();					
			}						
		}	
		currentFocus = null;
		nextFocus = null;
	};
	this.goNextBlock = function(keycode,focus){
		var activeBlock = this.getBlockById(focus.blockId);
		var nextFocus = new focusClass(focus.blockId,focus.elementId);
		var nextBlockId = null; 
		var nextBlockWay = TO_FIRST;
		switch(keycode){
			case VK_RIGHT:
				nextBlockId = activeBlock.nextBlockR;
				nextBlockWay = activeBlock.nextBlockR_way;
				break;
			case VK_LEFT:
				nextBlockId = activeBlock.nextBlockL;
				nextBlockWay = activeBlock.nextBlockL_way;
				break;
			case VK_UP:
				nextBlockId = activeBlock.nextBlockU;
				nextBlockWay = activeBlock.nextBlockU_way;
				break;
			case VK_DOWN:
				nextBlockId = activeBlock.nextBlockD;
				nextBlockWay = activeBlock.nextBlockD_way;
			break;
		}
		
		if(!nextBlockId){
			nextFocus = null;
			return false;
		}	
		
		nextFocus.blockId = nextBlockId;
		if(nextBlockWay == TO_FIRST){
			nextFocus.elementId = 0;			
		}
		else if(nextBlockWay == TO_LAST){
			nextFocus.elementId = this.getBlockById(nextFocus.blockId).listElements.length - 1;
		}
		else if(nextBlockWay == TO_SAME){						
			//nextFocus.elementId = focus.elementId;
			if(nextFocus.elementId>=this.getBlockById(nextFocus.blockId).listElements.length)
			{
				nextFocus.elementId = this.getBlockById(nextFocus.blockId).listElements.length-1;
			}								
		}								
		if(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId].style.display == 'none' 
			|| isClassNameExist(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId],CN_DISABLED)//~this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId].className.indexOf(CN_DISABLED) 	
			)
		{		
			//search in this block
			var candidateId = this.findNextElementInsideBlock(nextBlockId,nextBlockWay,nextFocus.elementId);		
				if(nextFocus.elementId == candidateId)//not found other not hidden element in this block
				nextFocus= this.findNextElement(keycode,nextFocus);			
			else			
				nextFocus.elementId = candidateId;			
		}	
		return nextFocus;
	};
	this.findNextElementInsideBlock = function(nextBlockId,nextBlockWay,firstCandidate){		
		var length = this.getBlockById(nextBlockId).listElements.length;		
		var elementId = firstCandidate;
		for(var i = 0; i < length; i++){		
			if(this.getBlockById(nextBlockId).listElements[i].style.display == 'none'
				||isClassNameExist(this.getBlockById(nextBlockId).listElements[i],CN_DISABLED)
				)
				{	
				;
			//	continue;
			}
			else{						
				if (firstCandidate != i){							
					if(Math.abs(firstCandidate - elementId) == 0)
						elementId = i; //setup any available for start										
					else 
						elementId = (Math.abs(firstCandidate - i) < Math.abs(firstCandidate - elementId))? i : elementId; //find nearest													
				}
			}
		}		
		return elementId;		
	};
	this.findNextElement = function(keycode,focus){	
		var activeBlock = this.getBlockById(focus.blockId);
		var nextFocus = new focusClass(focus.blockId,focus.elementId);
		switch(keycode)
		{
			case VK_RIGHT:	
				if(activeBlock.isHdirection){
					nextFocus.elementId++;
					if (nextFocus.elementId < activeBlock.listElements.length){
						if(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId].style.display == 'none'
							||isClassNameExist(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId],CN_DISABLED)
							)
							nextFocus = this.findNextElement(keycode,nextFocus);
						break;
					}
																
				}	
				nextFocus = this.goNextBlock(keycode,focus);										
				break;		
			
			case VK_LEFT:		
				if(activeBlock.isHdirection){
					nextFocus.elementId--;
					if (nextFocus.elementId >= 0){
						if(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId].style.display == 'none'
							||isClassNameExist(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId],CN_DISABLED)						
							)
							nextFocus = this.findNextElement(keycode,nextFocus);
						break;
					}
				}	
				nextFocus = this.goNextBlock(keycode,focus);										
				break;
				
			case VK_UP:	
				if(!activeBlock.isHdirection){
					nextFocus.elementId--;
					if (nextFocus.elementId >= 0){
						if(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId].style.display == 'none'
							||isClassNameExist(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId],CN_DISABLED)
							)
							nextFocus = this.findNextElement(keycode,nextFocus);
						break;
					}
				}	
				nextFocus = this.goNextBlock(keycode,focus);										
				break;
			case VK_DOWN:
				if(!activeBlock.isHdirection){
					nextFocus.elementId++;
					if (nextFocus.elementId < activeBlock.listElements.length){
						if(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId].style.display == 'none'
							||isClassNameExist(this.getBlockById(nextFocus.blockId).listElements[nextFocus.elementId],CN_DISABLED)
							)
							nextFocus = this.findNextElement(keycode,nextFocus);
						break;
					}
				}	
				nextFocus = this.goNextBlock(keycode,focus);										
				break;					
		}		
		return nextFocus;
	};
	this.initialMouseOverForAll = function(){	
		for(var i = 0; i < this.blockArray.length ;i++){
			this.blockArray[i].initialMouseOver();
		}		
	};	
	
	var BlockListClassPointer = this; 
   this.blockListOnMouseOff = function(event){       
        BlockListClassPointer.activateBlockListFocus();
        if(LGonMouseOff)
            LGonMouseOff(event);
    };
    this.blockListOnMouseOn = function(event){        
        BlockListClassPointer.releaseBlockListFocus();   
        if(LGonMouseOn)
            LGonMouseOn(event);
    };
	
	this.blockListOnMouseMove = function(event){			

	};
	this.onMRCUout = function(){	
		BlockListClassPointer.releaseBlockListFocus();	
	};	

	this.processKeyDown = function(event){		         
        switch(event.keyCode)
		{
			case VK_RIGHT:		
			case VK_LEFT:	
			case VK_UP:
			case VK_DOWN:
				BlockListClassPointer.setFocusOnNextElement(event.keyCode);				 
				break;
			case VK_ENTER:				
				BlockListClassPointer.getActiveBlock().getFocussedElement().onclick();											
				break;
		}       
              //  console.log("LGBlockNavigation_V2.2.js: key event ="+event.keyCode);
		return false;
	};
	/*Set active focus by BlockList.setFocusToElement([blockId],[buttonId]); after delete block*/
	this.deleteBlock = function(id){
		var index = this.getBlockIndexById(id);
		if(index == null){
			return;
		}
			
		for (var i = 0; i< this.blockArray.length; i++){
			this.blockArray[i].deleteRelationBlock(id);		
		}
		this.blockArray[index].removeMouseOver();
		if(this.blockArray[index].isBlockActive)
		{			
			this.blockArray.splice(index, 1);			
			this.blockArray[0].isBlockActive = true; //set active
		}
		else
		{
			this.blockArray.splice(index, 1);			
		}
	};	
	return this;
}; //End of BlockListClass
/****************************************************************************************************************************/
var BlockClass = function(id,direction,objectList,BlockListClassPointer){
	this.blockId = id;
	this.isHdirection = direction; // 1 - horizontal; 0-vertical
	this.listElements = objectList.slice();
	this.selectedList = [];
	this.currentFocus = 0;
	this.isBlockActive = false;
	//Link to next block
	this.nextBlockL = 0;
	this.nextBlockR = 0;
	this.nextBlockU = 0;
	this.nextBlockD = 0;
	
	this.nextBlockL_way = TO_FIRST; //TO_LAST//TO_SAME
	this.nextBlockR_way = TO_FIRST;
	this.nextBlockU_way = TO_FIRST; 
	this.nextBlockD_way = TO_FIRST;
	
	//set next block navigation by one step. Left/Right/Up/Down
	this.setNextBlockLRUD = function(L,R,U,D){
		this.nextBlockL = L ? L : 0;
		this.nextBlockR = R ? R : 0;
		this.nextBlockU = U ? U : 0;
		this.nextBlockD = D ? D : 0;		
	};
	this.setNextBlockWayLRUD = function(L,R,U,D){
		this.nextBlockL_way = (L==TO_LAST || L==TO_SAME) ? L : TO_FIRST;
		this.nextBlockR_way = (R==TO_LAST || R==TO_SAME) ? R : TO_FIRST;
		this.nextBlockU_way = (U==TO_LAST || U==TO_SAME) ? U : TO_FIRST;
		this.nextBlockD_way = (D==TO_LAST || D==TO_SAME) ? D : TO_FIRST;
	};
	this.getElementById = function(object){
		for(var i = 0; i < this.listElements.length; i++){
			if(~this.listElements[i].id.indexOf(object.id))			
				return this.listElements[i];			
		}
	};
	this.setCurrentFocus = function(object){
		for(var i = 0; i < this.listElements.length;i++){			
			if(~this.listElements[i].id.indexOf(object.id)){	            
				this.currentFocus = i;		
				return;
			}
		}
	};
	this.setDisabledElement = function(object){
		for(var i = 0; i < this.listElements.length;i++){			
			if(~this.listElements[i].id.indexOf(object.id)){		
				setClassName(this.listElements[i],CN_DISABLED);		
				return;
			}
		}
	};
	this.setEnabledElement = function(object){
		for(var i = 0; i < this.listElements.length;i++){			
			if(~this.listElements[i].id.indexOf(object.id)){		
				setClassName(this.listElements[i],CN_IDLE);		
				this.removeFromSelectedList(object);
				return;
			}
		}
	};
	this.setSelectedElement = function(object){
		var exist=false;
		for(var i = 0; i < this.selectedList.length; i++ ){
			if(~this.selectedList[i].id.indexOf(object.id)){
				exist = true;
				break; //already exist
			}
		}	
		if(!exist)
			this.selectedList.push(object);
		setClassName(object,CN_SELECTED); 

	};
	this.checkSelected = function(object){
		for(var i = 0; i < this.selectedList.length; i++ ){
			if(~this.selectedList[i].id.indexOf(object.id)){
				return true;				
			}
		}	
		return false;
	};

	this.removeFromSelectedList = function(object){		
		for(var i = 0; i < this.selectedList.length; i++ ){
			if(~this.selectedList[i].id.indexOf(object.id)){
				this.selectedList.splice(i,1);
				return;
			}
		}			
	};
	this.ifFocussed = function(object){
		if(this.isBlockActive){
			if(~this.listElements[this.currentFocus].id.indexOf(object.id)){			
					return true;
			}
		}
		return false;
	};
	this.getFocussedElement = function(){		
		return this.listElements[this.currentFocus];
	};
	this.releaseBlockFocus = function(){	
            if (isClassNameExist(this.listElements[this.currentFocus],CN_DISABLED))
                return;
            
            if(this.checkSelected(this.listElements[this.currentFocus]))
                setClassName(this.listElements[this.currentFocus],CN_SELECTED);	                
            else			
                setClassName(this.listElements[this.currentFocus],CN_IDLE);		
	};
	this.activateBlockFocus = function(){
		this.setActiveBlock();        
		setClassName(this.listElements[this.currentFocus],CN_FOCUS);
	};	
	this.initialMouseOver = function(){		
		for(var i = 0; i < this.listElements.length;i++){					
			this.listElements[i].onmouseover = this.onMRCUhover;			
			this.listElements[i].onmouseout = this.onMRCUout;	
			//initial idle status for each navigated element except focussed
			if((!this.isBlockActive || this.currentFocus != i) && !isClassNameExist(this.listElements[i],CN_DISABLED))
				setClassName(this.listElements[i],CN_IDLE);
		}		
	};
	this.removeMouseOver = function(){
		for(var i = 0; i < this.listElements.length;i++){					
			this.listElements[i].onmouseover = "";			
			this.listElements[i].onmouseout = "";					
		}
	};
	var BlockClassPointer = this; //save for using in onMRCUhover	
	this.onMRCUhover = function(){	
		if(isMRCUOn()){
			if(!isClassNameExist(this,CN_DISABLED))
			{
				BlockListClassPointer.releaseBlockListFocus();
				BlockClassPointer.setActiveBlock();
				BlockClassPointer.setCurrentFocus(this); 
				BlockListClassPointer.activateBlockListFocus();				
			}
		}
	};
	this.setActiveBlock = function(){
		BlockListClassPointer.getActiveBlock().isBlockActive = false;
		this.isBlockActive = true;
	};
    this.setAsScrollBlock = function(maxVisible){
        if(maxVisible + 2 >= this.listElements.length){ //+2 arrows
            this.setDisabledElement(this.listElements[0]);
            this.setDisabledElement(this.listElements[this.listElements.length-1]);
        }
        else{
            this.setDisabledElement(this.listElements[0]);
            for(var i = maxVisible+1; i < this.listElements.length -1; i++){
                this.listElements[i].style.display = "none";
            }     
            this.listElements[0].onclick = this.scrollBack;
            this.listElements[this.listElements.length-1].onclick = this.scrollForward;
        }
    };      
    this.scrollForward = function(){
         var arrowBack = 0;
         var elFirst = 1;
         var arrowForward = BlockClassPointer.listElements.length - 1; 
         var elLast = arrowForward -1;
         var start = 0;
         var end = 0;
         for(var i = elFirst; i <= elLast ; i++){
             
             if(start == 0 && BlockClassPointer.listElements[i].style.display != 'none'){                        
                 start = i;
             }
          //   alert("i=" + i+" BlockClassPointer.listElements[i].style.display=" +BlockClassPointer.listElements[i].style.display +"start = "+start)  
             if(start>0 && end==0 && BlockClassPointer.listElements[i].style.display == 'none'){                 
                 end = i;                 
             }             
         }                
         if(start == 0){                     
            return; //error
         }
         
         if(end == 0){
            BlockClassPointer.setDisabledElement(BlockClassPointer.listElements[arrowForward]);  
            return; //no way to scroll forward
         }  
                              
         if(end <= elLast){                        
             BlockClassPointer.listElements[end].style.display = "";
             BlockClassPointer.listElements[start].style.display = "none";                             
         }
         if(start == elFirst)
            BlockClassPointer.setEnabledElement(BlockClassPointer.listElements[arrowBack]);
            
         if(end >= elLast){
            BlockClassPointer.setDisabledElement(BlockClassPointer.listElements[arrowForward]);       
            BlockClassPointer.setCurrentFocus(BlockClassPointer.listElements[elLast]);
            if(!isMRCUOn()){
                BlockClassPointer.activateBlockFocus();
            }          
         }                          
         
    };
    this.scrollBack = function(){
         var arrowBack = 0;
         var elFirst = 1;
         var arrowForward = BlockClassPointer.listElements.length - 1; 
         var elLast = arrowForward -1;
         var start = 0;
         var end = 0;
         for(var i=elFirst; i<=elLast ; i++){               
             if(start == 0 && BlockClassPointer.listElements[i].style.display != 'none'){                        
                 start = i;
             }          
             if(start>0 && end==0 && BlockClassPointer.listElements[i].style.display == 'none'){                 
                 end = i;                 
             }             
         }                
         if(start == 0){                     
            return; //error
         }
         
         if(end == 0){
            end = elLast+1; //-1 will be done later             
         }                   
         if(start > elFirst){                        
             BlockClassPointer.listElements[start-1].style.display = "";
             BlockClassPointer.listElements[end-1].style.display = "none";
         }
         if(end == elLast) //enable arrow one time
            BlockClassPointer.setEnabledElement(BlockClassPointer.listElements[arrowForward]);
          
         if(start - 1 == elFirst){ //disable arrowBack
            BlockClassPointer.setDisabledElement(BlockClassPointer.listElements[arrowBack]);  
            BlockClassPointer.setCurrentFocus(BlockClassPointer.listElements[elFirst]);
            if(!isMRCUOn()){
                BlockClassPointer.activateBlockFocus();
            }           
         }                          
    };
	this.onMRCUout = function(){	
		BlockListClassPointer.releaseBlockListFocus();	
	};

	this.deleteRelationBlock = function(id){
        if (this.nextBlockL != 0 && ~this.nextBlockL.indexOf(id)) {
			this.nextBlockL = 0;
			this.nextBlockL_way = TO_FIRST;			
		}
        if (this.nextBlockR != 0 && ~this.nextBlockR.indexOf(id)) {
			this.nextBlockR = 0;
			this.nextBlockR_way = TO_FIRST;		
		}
        if (this.nextBlockU != 0 && ~this.nextBlockU.indexOf(id)) {
			this.nextBlockU = 0;
			this.nextBlockU_way = TO_FIRST;	
		}
        if (this.nextBlockD != 0 && ~this.nextBlockD.indexOf(id)) {
			this.nextBlockD = 0;
			this.nextBlockD_way = TO_FIRST;		
		}
	};
	return this;	
};
