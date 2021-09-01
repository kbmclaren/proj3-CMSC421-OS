/* file: reversi.c
* author: Caleb M. McLaren
* email: mclaren1@umbc.edu
* date: April 23rd, 2021
* description: This linux character device driver must implement the game reversi
*	a.k.a. Othello. 
*/

#include <linux/module.h>   /* needed by all modules, for THIS_MODULE*/
#include <linux/kernel.h>   /* KERN_INFO and copy_from_user*/
#include <linux/init.h>     /* for __init and __exit*/
#include <linux/err.h>      /* for IS_ERR*/
#include <linux/cdev.h>     /* for embedding cdev in reversi_data, code defined at fs/char_dev.c, includes kdev_t.h */
#include <linux/fs.h>       /* for file_operations */
#include <linux/semaphore.h>      /* lock and unlock at open and release || tried include/asm/semaphore. no work*/
#include <linux/device.h>	/* needed for device_create etc */
#include <linux/slab.h> 	/* for kfree and kmalloc*/
#include <linux/ctype.h>	/* needed for isspace */
#include <linux/uaccess.h>	/* copy_from_user */
#include <linux/errno.h> 	/* for ERRORs */
#include <linux/random.h>	/* for computer choice */
#include <linux/init.h>		/* for MAJOR*/

#define REVERSI_MAX_MINORS	1
#define DEVICE_NAME "reversi"
#define DEVICE_CLASS "reversiClass"
#define AUTHOR "Caleb M. McLaren <mclaren1@umbc.edu>"
#define MOD_DESCRIPTION "The game reversi, a.k.a Othello."
#define BLACK "X"
#define WHITE "O"
#define EMPTY "-"
#define COM00 "00\0"
#define COM01 "01\0"
#define COM02 "02\0"
#define COM03 "03\0"
#define COM04 "04\0"
#define WIN "WIN\n"
#define TIE "TIE\n"
#define LOSE "LOSE\n"
#define OK "OK\n"
#define NOGAME "NOGAME\n"
#define ILLMOVE "ILLMOVE\n"
#define OOT "OOT\n"
#define UNKCMD "UNKCMD\n"
#define INVFMT "INVFMT\n"
/* #define DIRECTIONS [8] = {-11, -10, -9, -1, 1, 9, 10, 11} */
#define BOARDSIZE 100


MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION(MOD_DESCRIPTION);

/*
* Gobal Variables
*/ 
static struct class *reversi_class = NULL;
static dev_t majMinor;
static DEFINE_SEMAPHORE(mr_mutex);
static int DIRECTIONS[] = {-11, -10, -9, -1, 1, 9, 10, 11};
/*static enum colRow { CR0, CR1, CR2, CR3, CR4, CR5, CR6, CR7 };*/

/*
* Prototypes - have to be before file operations for some reason.
*/
/* Parsing*/
size_t count_spaces(const char *str);
void flush_string( char * cp);
void simpleParse(char * theCmd, char * tokenArray []);

/*Othello*/
char * setupBoard(void);
char * returnOpponent(char * player);
int countToken(char * player, char * board);
int validMove(int move);
int lookForBracket(int boardSquare, char * player, char * board, int dir);
int lookForFlip(int move, char * player, char * board, int dir);
int checkForLegal(int move, char * player, char * board);
void flipTokens(int move, char * player, char * board, int dir);
void makeYourMove(int move, char * player, char * board );
int lookForLegalMove(char * player, char * board);
char * checkNextPlayer( char * board, char * prevPlayer);
unsigned int chooseRandomMove( char * player, char * board);
int figureWhoWon(char * player, char * board);

/*VFS*/
static int reversi_open(struct inode *inode, struct file *f);
static int reversi_release(struct inode *inode, struct file *f);
static ssize_t reversi_read(struct file *f, char __user *buf, size_t len, loff_t *off);
static ssize_t reversi_write(struct file *f, const char __user *cmd, size_t len, loff_t *off);

/* define file_operations */
const struct file_operations reversi_fops = {
	.owner = THIS_MODULE,
 	.read = reversi_read,
 	.write = reversi_write,
 	.open = reversi_open,
 	.release = reversi_release
};

struct reversi_data {
	/*
	* recall that include/linux/cdev has four structs inside
	* kobjects, module, file_operations, list_head
	* WE USE FILE_OPERATIONS inside reversi_cdev
	* and cdev structs are used by the kernel to represent char devices internally
	*/
	struct cdev reversi_cdev;
    char * the_board; /* pointer to reversi board*/
	char * humanToken; 
	char * computerToken;
    char * feedbackString;
    char * prevPlayer; 
    int score; 
} devs;

/*
* helper/game functions
*/

/*
* take in nothing
* return char * to initial game board
*/
char * setupBoard(void) {
	int i;
	char * board = NULL;  

	board = (char *)kmalloc(BOARDSIZE * sizeof(char), GFP_KERNEL);
	if (board == NULL){
		printk(KERN_ALERT "In setupBoard, kmalloc failed\n");
		return board;
		}

	/*build boarder and starting board*/
	/*I just need 60 empty board spots*/
	/* for ( i = 0 ; i <= 9; i++) { board[i] = "#"; } */
	for ( i = 0 ; i <= 9; i++) { strncpy( &board[i], "#", 1) ; }
	for ( i = 10; i <= 89; i++){
		if(i%10 >= 1 && i%10 <= 8){
			strncpy( &board[i], "-", 1) ;
		}
		else{
			strncpy( &board[i], "#", 1) ;
		}
	}
	for (i = 90; i <=99; i ++) {
		strncpy( &board[i], "#", 1) ;
	}
	strncpy( &board[44], "O", 1) ; 
	strncpy( &board[45], "X", 1) ;
	strncpy( &board[54], "O", 1) ; 
	strncpy( &board[55], "X", 1) ;
	return board; 
}

/* 
* Copied from project 1, file utils.c. 
* Utils.c was provided by CMSC 421 project creators, 
* Spring 2021, UMBC
 */
size_t count_spaces(const char *str) {
    size_t rv = 0;

    while(*str) {
        if(isspace(*str++))
            ++rv;
    }

    return rv;
}

/*
* Copied from project 1, file main.c. 
* Original authored by Caleb M. McLaren (myself) for project 1,
* CMSC 421, Spring 2021, UMBC.
*/
void flush_string( char * cp){
	char * temp; 
	if( (temp = strchr(cp, '\n')) != NULL)
    *temp = '\0';
}

/*simple parse*/
void simpleParse(char * theCmd, char ** tokenArray){
	char * temp;
	int i = 0; 
	flush_string(theCmd);
    printk("%s\n", theCmd);

	while((temp = strsep(&theCmd, " ")) != NULL){
        printk("%s\n", temp);
		tokenArray[i] = temp;
        printk("%s\n", tokenArray[i]); 
		i++;
	}
	i++; 
	tokenArray[i] = '\0'; 
}

/*
* Take in Player
* Return opposite player or null.
*/
char * returnOpponent(char * player) {
	char * opp = NULL;
	if (strcmp(player, "X") == 0){
		opp = "O";
		return opp;
	}
	else if (strcmp(player, "O") == 0){
		opp = "X";
		return opp;
	}
	else {
		printk("%s\n", "Illegal Player");
		return opp;
	}
}

/*
* take in square on board, 
* check in direction for bracket
* return bracket location if found
*/
int lookForBracket(int boardSquare, char * player, char * board, int dir) {
	/* as long as the content of boardSquare matches the opponent token
	* we keep looking for the bracket. If we find empty board square or the edge of the board,
	# we stop while loop. */
	while ( strncmp(&board[boardSquare], returnOpponent(player), 1 ) {
		boardSquare += dir;
	}
	/*if the current board square respective player token, we have found the bracket*/
	if( strncmp(&board[boardSquare], player, 1) ) 
	{
		return boardSquare; 
	}
	else {
		return 0; 
	}
}

/* 
* take in move and direction, 
* check if neighbor square is an opponent token
* return bracket location or zero 
*/
int lookForFlip(int move, char * player, char * board, int dir){
	int neighborSquare; 
	neighborSquare = move + dir;
	if (board[neighborSquare] == *returnOpponent(player)) {
		return lookForBracket((neighborSquare + dir), player, board, dir);
	}
	return 0; 
}

/*
* take in move, 
* check for valid move, 
* check for availability of move, 
* when move available, check for flips
* no flips, return illegal move
* flips found, return legal move 
*/
int checkForLegal(int move, char * player, char * board) 
{
	int i = 0; 
	char empty[] = "-";
	/*check if move is on the board*/
	if(( move <= 88) && (move >= 11) && ( move%10 <= 8 ) && ( move%10 >= 1)) 
	{
		/*check if move is occupied*/
		if( strncmp( &board[move], empty, 1 ))
		{
			i = 0; 
			while( i <= 7 && !lookForFlip(move, player, board, DIRECTIONS[i]))
			{
				i++;
			}
			if( i == 8 )
			{
				/*checked all directions and no flips possible, illegal move*/
				return 0; 
			} else 
			{
				/*flip possible in at least one direction, legal move*/
				return 1; 
			}
		}
		/*move IS occupied, illegal move*/
		else 
		{
			return 0; 
		}
	}
	/*Move not on the board*/
	return 0; 
}

/*
* call after lookForLegal,
* take in move, player, board, and direction,
* determine bracket location,
* flip opponent tokens 
*/
void flipTokens(int move, char * player, char * board, int dir){
	int whereMyBracket, neighborSquare;

	/*get board square that brackets move*/
	whereMyBracket = lookForFlip(move, player, board, dir);

	/*if bracket found, flip tokens in that direction*/
	if ( whereMyBracket ) {
		neighborSquare = move + dir;
		do {
			board[neighborSquare] = *player; 
			neighborSquare = neighborSquare + dir;
		} while (neighborSquare != whereMyBracket);
	}
}

/* 
* call after checkForLegal
* Take in move, player, board.  
* Call flipTokens in all directions 
*/
void makeYourMove(int move, char * player, char * board ) {
	int i;
	board[move] = *player; 

	/*in all directions check for bracket and flip tokens when bracket found */
	for (i = 0; i <= 7; i++){
		flipTokens(move, player, board, DIRECTIONS[i]);
	}
}

/*
* take in player and board,
* call lookForLegal for every spot on board
* return 1 if even one legal move is found for player
*/
int lookForLegalMove(char * player, char * board) {
	int move;
	move = 88;

	/*check for legal moves, 
	* while loop stops when lookForLegal returns 1 */
	while (move >= 11 && !checkForLegal(move, player, board)){
		move--; 
	}
	if (move >= 11){
		return 1; 
	} else {
		return 0; 
	}
}

/*
* take in board and previous player,
* check if next/previous player has even 1 legal move.
* return next player, previous player, or cat as appropriate.   
*/
char * checkNextPlayer( char * board, char * prevPlayer){
	char * cat = NULL;
	char * opponent = NULL;
	opponent = returnOpponent(prevPlayer);
	if (lookForLegalMove(opponent, board)) {
		return opponent; 
	}
	if (lookForLegalMove(prevPlayer, board)) {
		return prevPlayer;
	}
	cat = TIE;
	return cat; 
}

/*
* take in player and board,
* kmalloc to hold all legal moves for player
* return int pointer
*/
int * tallyLegalMoves (char * player, char * board){
	int move, i; 
	int * movesList;
	movesList = (int *)kmalloc(65 * sizeof(int), GFP_KERNEL);
	movesList[0] = 0; 
	i = 0;
	for (move = 88; move >= 11; move--){
		if(checkForLegal(move, player, board)) {
			i++; 
			movesList[i] = move; 
		}
	}
	/* how many moves did we find?*/
	movesList[0] = i;
	return movesList; 
}

/*
* Take in player and board.
* Call tallyLegalMoves for respoective player.
* Free memory allocated in tallyLegalMoves.
* Return random choice of viable moves. 
*/
unsigned int chooseRandomMove( char * player, char * board){
	unsigned int randChoice;
	int * movesList;
	movesList = tallyLegalMoves(player, board);
	randChoice = movesList[((get_random_int()%movesList[0]) + 1)];
	kfree(movesList); 
	return randChoice;
}

/*
* take in player and board.
* iterate through board, counting respective pieces
* return difference. Positive return = player win.
* Negative return = computer win.
*/
int figureWhoWon(char * player, char * board){
	int i, opponentCount, playerCount;
	char * opponent;
	opponentCount = 0;
	playerCount = 0; 
	opponent = returnOpponent(player); 
	for( i = 11; i <= 88; i++) {
		if (board[i] == *player) {
			playerCount++;
		}
		if (board[i] == *opponent ){
			opponentCount++;
		}
	}
	return (playerCount - opponentCount);

}

/*
*  open, release, read, and write
*/
static int reversi_open(struct inode *inode, struct file *f)
{
	
    if( down_trylock(&mr_mutex) ) 
	{
		/*contended*/
		printk(KERN_ALERT "reversi open() did not get mutex\n");
    	return 0;
	}
    /*grab pointer to devs.reversi_cdev*/
	/* struct reversi_data *my_device_info = 
			container_of(inode->i_cdev, struct reversi_data, cdev); 
			
	f->private_data = my_device_info; */
	
	/*Set up device*/
	devs.the_board = NULL;
	devs.humanToken = NULL; 
	devs.computerToken = NULL;
    devs.feedbackString = NULL;
    devs.prevPlayer = NULL; 
    devs.score = 0;

	/* devs.the_board = NULL;
	f->private_data->humanToken = NULL; 
	f->private_data->computerToken = NULL;
    f->private_data->feedbackString = NULL;
    f->private_data->prevPlayer = NULL; 
    f->private_data->score = 0;  */
    printk(KERN_INFO "reversi Driver: open()\n");
    return 0;
}
static int reversi_release(struct inode *inode, struct file *f)
{
    up(&mr_mutex);
    printk(KERN_INFO "reversi Driver: close()\n");
    return 0;
}

static ssize_t reversi_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    /*build return string*/
    if( copy_to_user(buf, devs.feedbackString, len) == 0){
        printk(KERN_INFO "Good reversi_read\n");
        return len; 
    } else {
        printk(KERN_ALERT "Reversi failed to register a major number\n");
        return -EFAULT; 
    }
}
static ssize_t reversi_write(struct file *f, const char __user *cmd, size_t len, loff_t *off)
{
    int dam, compMove, score, humanMove; 
	char term[] = "\0";
	char ** tokenArray = NULL;
	char * nextPlayer = NULL; 
	int * col = NULL;
	int * row = NULL;  
	/*take any size command*/
	char * the_cmd = NULL;
	the_cmd = (char *)kmalloc(len*sizeof(char), GFP_KERNEL);
	printk(KERN_INFO "reversi-basic Driver: write()\n");

	/*check for bad memory allocation*/
	if ( the_cmd == NULL ) 
	{
		return -ENOMEM; 
	}

	/*check for good copy*/
	if ( copy_from_user(the_cmd, cmd, len*sizeof(char)) == 0 ) 
	{
		/*Set aside appropriately sized token array */
		dam = count_spaces(the_cmd); 
        printk(KERN_INFO "%d\n", dam);
		
		/*too many/few commands*/
		if ( dam > 3 || dam == 0)
		{
			return -EINVAL; 
		}

		tokenArray = kmalloc((dam + 1)*sizeof(char *), GFP_KERNEL);

		/*parse the_cmd*/
		simpleParse(the_cmd, tokenArray);
		kfree(the_cmd);

		/*interpret parsed command*/
		if ( !strcmp( tokenArray[0], COM00) || !strcmp(tokenArray[0], COM01) ||
			 !strcmp(tokenArray[0], COM02) || !strcmp(tokenArray[0], COM03) || 
			 !strcmp(tokenArray[0], COM04)) 
        {
			if ( !strcmp(tokenArray[1], term)) 
			{
				if(strcmp(tokenArray[0], COM01) == 0 ) 
				{
					/*Returns the current state of the game board...*/
					kfree(tokenArray);
                    devs.feedbackString = devs.the_board; 
                    return len; 
				}
				else if (strcmp(tokenArray[0], COM03) == 0)
				{
					kfree(tokenArray);
                    /*check for NOGAME*/
                    if (devs.the_board == NULL)
					{
                        devs.feedbackString = NOGAME;
						return len;
                    }

                    /* 
                    * check for computers turn 
                    * if computer has move, done
                    * if no move available for computer, return OOT
                    * if CAT check for win*/
                    /* checkNextPlayer guarantees that nextPlayer has a legal move availabe*/
					nextPlayer = checkNextPlayer( devs.the_board, devs.prevPlayer);
                    if ( strcmp ( nextPlayer, devs.computerToken ) == 0 ) 
					{
                        /*previous player was human */
                        /*there is a move available to the computer, do not skip computer*/
                        devs.prevPlayer = devs.computerToken; 
                        /* Computer choose its move from legal moves or returns 0. */
					    compMove = chooseRandomMove(devs.computerToken, devs.the_board);
                        makeYourMove(compMove, devs.prevPlayer, devs.the_board);
                        devs.feedbackString = OK;  
                        return len; 
                    } 
                    else if ( strcmp(nextPlayer, devs.humanToken) == 0 )
					{
                        /*computer has no legal move availabe, but human does*/
                        devs.prevPlayer = devs.humanToken;
                        devs.feedbackString = OOT;
                        return len; 
                    }
                    else 
					{
                        /* no legal moves available to human or computer */
                        /*always tally with respect to human perspective*/
                        score = figureWhoWon(devs.humanToken, devs.the_board);
                        devs.score = score;
                        if (score > 0 ) 
						{
                            devs.feedbackString = WIN;
							kfree(devs.the_board);
							return len;
                        }
                        else if (score == 0 ) 
						{
                            devs.feedbackString = TIE;
							kfree(devs.the_board);
							return len;
                        }
                        else 
						{
                            devs.feedbackString = LOSE;
							kfree(devs.the_board);
							return len;
                        }    
                    }
				}
                /* 
                * human believes he has no moves left
                * check if human turn
                * check if human has moves left
                */
				else if ( strcmp(tokenArray[0], COM04) == 0){

					kfree(tokenArray);
                    /*
                    * prevPlayer normally computer, 
                    * Expect return human token since computer did not declare game over.
                    * If return computer, then no legal move available for human */
                    nextPlayer = checkNextPlayer(devs.the_board, devs.prevPlayer);

                    if (strcmp(nextPlayer, devs.computerToken) == 0){
                        /*human was right and had no moves available,*/
                        /*computer has moves left, game continues*/
                        devs.prevPlayer = devs.humanToken; 
                        devs.feedbackString = OK;
						return len; 
                    }
                    else if (strcmp(nextPlayer, devs.humanToken) == 0){
                        /*human was wrong and still has a move left*/
                        devs.feedbackString = ILLMOVE;
						return len;
                    }
                    else {
                        /*human has no moves available and neither does the computer*/
                        /*always tally with respect to human perspective*/
                        score = figureWhoWon(devs.humanToken, devs.the_board);
                        devs.score = score;
                        if (score > 0 ) {
                            devs.feedbackString = WIN;
							kfree(devs.the_board);
							return len;
                        }
                        else if (score == 0 ) {
                            devs.feedbackString = TIE;
							kfree(devs.the_board);
							return len;
                        }
                        else {
                            devs.feedbackString = LOSE;
							kfree(devs.the_board);
							return len;
                        }    
                    }
				}
			} 
            /* 
            * Command has one argument, and second argument must be null to be formatted correctly
            */
			else if (!strcmp(tokenArray[2], term))
			{
                /* human chooses black*/
				if(strcmp(tokenArray[1], BLACK) == 0){
                    /*
                    * set/reset board
                    * pretend that previous player was computer
                    * set response string to OK
                    */

				   	if (devs.the_board != NULL)
					{
						kfree(devs.the_board);
					}
                    devs.the_board = setupBoard(); 
                    devs.humanToken = BLACK;
                    devs.computerToken = WHITE;
                    devs.feedbackString = OK;
                    devs.prevPlayer = devs.computerToken;
					kfree(tokenArray); 
					return len; 

                }
                /*human chooses white*/
                else if ( strcmp(tokenArray[1], WHITE) == 0){
                    /*
                    * set/reset board
                    * pretend that previous player was human
                    * set response string to OK*/

				   	if (devs.the_board != NULL)
					{
						kfree(devs.the_board);
					}
                    devs.the_board = setupBoard(); 
                    devs.humanToken = WHITE;
                    devs.computerToken = BLACK;
                    devs.feedbackString = OK;
                    devs.prevPlayer = devs.humanToken;
					kfree(tokenArray);
					return len;  
                }
			}
			/*
			* command has two arguments
			*/
			else if (!strcmp(tokenArray[3], term)) 
			{
				/* good format of command */
                if ( kstrtoint(tokenArray[1], 10, col) == 0 && kstrtoint( tokenArray[2], 10, row ) == 0 ) 
				{

					/* check for user turn*/
					if ( strcmp ( nextPlayer = checkNextPlayer( devs.the_board, devs.prevPlayer), 
							devs.humanToken ) == 0 ) 
					{
						/*nextPlayer is human and a move exists for human*/
						devs.prevPlayer = devs.humanToken; 
						
						/*
						* convert human choice to board coordinate
						* Add 1 to col & row; col * 10; sum col and row. 
						*/
						humanMove = (((*col+1)* 10 ) + (*row + 1));
						if ( checkForLegal(humanMove, devs.prevPlayer, devs.the_board) )
						{
							makeYourMove(humanMove, devs.prevPlayer, devs.the_board);
							devs.feedbackString = OK;  
							return len;
						} 
						else{
							devs.feedbackString = ILLMOVE;  
							return len;
						}
						
                    } 
                    else if ( strcmp( nextPlayer, devs.computerToken) == 0 )
					{
                        /*human has no legal move availabe, but computer does*/
                        devs.prevPlayer = devs.humanToken;
                        devs.feedbackString = OOT;
                        return len; 
                    }
                    else 
					{
                        /* no legal moves available to human or computer */
                        /*always tally with respect to human perspective*/
                        score = figureWhoWon(devs.humanToken, devs.the_board);
                        devs.score = score;
                        if (score > 0 ) 
						{
                            devs.feedbackString = WIN;
							kfree(devs.the_board);
							return len; 
                        }
                        else if (score == 0 ) 
						{
                            devs.feedbackString = TIE;
							kfree(devs.the_board);
							return len;
                        }
                        else 
						{
                            devs.feedbackString = LOSE;
							kfree(devs.the_board);
							return len;
                        }    
                    }
				}
				else 
				{
					/*bad format of command*/
					kfree(tokenArray);
					devs.feedbackString = INVFMT;
					return len; 
				}
			}
			else 
			{
				/*bad format of command*/
				kfree(tokenArray);
				devs.feedbackString = INVFMT;
				return len; 
			}
		}
		else 
		{
            /* 
            * "UNKCMD\n"
            */
			kfree(tokenArray);
            devs.feedbackString = UNKCMD;
            return len; 
		}
	 
	} 
	else 
	{
		/*unable to copy command*/
        printk(KERN_ALERT "Bad copy from user in reversi_write\n");
		return -EFAULT;
	}	
    /*should never get here*/
    return -EFAULT;
}

/*
* specify default permissions
*/
static int reversi_uevent(struct device *dev, struct kobj_uevent_env *env){
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0; 
}

/*
* init and exit 
*/
static int __init init_reversi(void){
	int err, check, reversi_dev_major; 
    struct device *dev_ret; 

	/*dynamic allocation major number to character device, flexible */
    /*error check*/
	err = alloc_chrdev_region(&majMinor, 
								0, 
								1,
								DEVICE_NAME); 

	if (err != 0) {
        printk(KERN_ALERT "Reversi failed to register a major number\n");
		return err;
	}

    /*this is the dyanmically allocated major number*/
	reversi_dev_major = MAJOR(majMinor);
    printk(KERN_INFO "Reversi device registered correctly with major number %d\n", reversi_dev_major);

	/*
	* Create sysfs class in order to access device from user space.
	* to see tree, print out w/ "tree /sys/devices/virtual/reversi/ 
    * check class pointer reversi_class for error
    * report success or return failure after clean up
	*/
	reversi_class = class_create(THIS_MODULE, DEVICE_CLASS);
    if(IS_ERR(reversi_class)){
        unregister_chrdev_region(majMinor, 1);
        printk(KERN_ALERT "Failed to register reversi_class\n");
        return PTR_ERR(reversi_class);
    }
    printk(KERN_INFO "Reversi_class registered correctly\n");

    /*set the permissions for the new file available in user space*/
	reversi_class->dev_uevent = reversi_uevent;

    /*create device node, found at /dev/reversidev-x, replace x with "i" of Minor number*/
    dev_ret = device_create(reversi_class, NULL, majMinor, NULL,
                                DEVICE_NAME);
    if(IS_ERR(dev_ret)){
            printk(KERN_ALERT "Failed to create reversi device\n");
            class_unregister(reversi_class);
            class_destroy(reversi_class);
            unregister_chrdev_region(majMinor, 1);
            return PTR_ERR(dev_ret); 
    }
    printk(KERN_INFO "Reversi device created\n");

    cdev_init(&devs.reversi_cdev, &reversi_fops);
    devs.reversi_cdev.owner = THIS_MODULE;
    //devs[i].reversi_cdev.ops = &reversi_fops; //this happens during cdev_init
    
    /*Add device to system = live immediately. "i" is the Minor number of the new device.*/
    /*check for rare cdev_add failure*/
    check = cdev_add(&devs.reversi_cdev, majMinor, 1);
    if (check) {
        printk(KERN_ALERT "Error %d adding %s", check, DEVICE_NAME);
        device_destroy(reversi_class, majMinor);
        class_destroy(reversi_class);
        unregister_chrdev_region(majMinor, 1);
        return check; 
    }
    printk(KERN_INFO "Reversi device added to kernel correctly\n");

    printk(KERN_INFO "init_reversi complete");
	return 0;
}

static void __exit cleanup_reversi(void){ 
 	printk(KERN_INFO "cleanup_reversi started");
 	
    /*delete device from system
    * destroy device
    * unregister class
    * destroy class
    * release major and minor number*/
    cdev_del(&devs.reversi_cdev);
    printk(KERN_INFO "cdev_del FINISHED 1");

    device_destroy(reversi_class, majMinor);
    printk(KERN_INFO "device_destroy FINISHED 2");

    class_unregister(reversi_class);
    printk(KERN_INFO "class_unregister FINISHED 3");

 	class_destroy(reversi_class);
    printk(KERN_INFO "class_destroy FINISHED 4");

 	unregister_chrdev_region(majMinor, 1);
    printk(KERN_INFO "cleanup_reversi FINISHED DONE"); 
}


module_init(init_reversi);
module_exit(cleanup_reversi);