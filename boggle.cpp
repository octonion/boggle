#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>

#include <atomic>

#include <boost/random.hpp>
#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

using namespace std;

const int threads=1;

const int rows=4;
const int columns=4;

const long total_shakes=1000000;
long shakes=0;
long my_shakes[threads];

const int n_cubes=16;
const int n_sides=6;

struct node {
  node *letters[26];
  string value;
  int length;
  unsigned long long int hits;
  bool unused;
  bool terminal;
};

typedef struct arg_struct {
  node *dawg;
  int thread;
}arg_struct_t;

int cubes[n_cubes][n_sides];

//int rows;
//int columns;

bool used[threads][rows][columns];
int grid[threads][rows][columns];
int total[threads];
node *found[threads][5000];

std::vector<int> perm[threads];
boost::random::mt19937 gen;
boost::random::uniform_int_distribution<> dist(0,5);

//static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void build_dawg(node *dawg, istream *word_file) {

  int i;
  int thread;
  int letter;
  node *current;
  string word;

  if (*word_file) {
    while (*word_file >> word) {

      i = 0;
      current = dawg;

      while (i < word.length()) {

	letter = word[i]-'a';

	current->terminal = false;

	if (current->letters[letter]==NULL) {
	  node *newnode = new node;
  	  current->letters[letter] = newnode;
	  current = current->letters[letter];
	  current->value = "*";
	}
	else {
	  current = current->letters[letter];
	}

	if (word[i]=='q' & word[i+1]=='u')
	  i++;

	i++;

      }

      if (i > rows-2) {
	current->value = word;  
	current->length = i;

	for (thread=0; thread<threads; thread++) {
	  current->hits = 0;
	  current->unused = true;
	}
	current->terminal = true;
      }


    }
  }

}

void print_dawg(node *current) {

  int i;

  if ((!(current->value == "*")) and (current->hits>0))
      cout << current->value << "," << current->hits << endl;

  if (!current->terminal) {
    for (i=0; i < 26; i++) {
      if (!(current->letters[i]==NULL))
	print_dawg(current->letters[i]);
    }
  }

}

void find_all(node *current, int x, int y, int thread) {

  int dx,dy;
  int a,b;

  //cout << current->value << endl;

  if (!(current->value=="*") & (current->unused)) {

    current->unused = false;
    current->hits++;
    
    //score_1 = score_1 + scores_1[current->length];
    //score_2 = score_2 + scores_2[current->length];
    //score_3 = score_3 + scores_3[current->length];
    found[thread][total[thread]] = current;
    total[thread]++;
  }

  if (current->terminal)
    return; // void;

  for (dx=-1; dx<=1; dx++) {
    for (dy=-1; dy<=1; dy++) {

      if ((dx==0) and (dy==0))
	continue;

      a = x+dx;
      b = y+dy;

      if ((a<0) or (a>rows-1) or (b<0) or (b>columns-1))
	continue;

      if (used[thread][a][b])
	continue;

      if (current->letters[grid[thread][a][b]]==NULL)
	continue;

      used[thread][a][b] = true;
      find_all(current->letters[grid[thread][a][b]],a,b,thread);
      used[thread][a][b] = false;

    }
  }

}

void *sim(void *str) {

  int i;
  int j;
  int k;

  arg_struct_t *local_str = (arg_struct_t *)str;
  //struct arg_struct *c_arg = arguments;

  node *dawg =  local_str->dawg;
  int thread =  local_str->thread;

  //cout << thread << ": simming " << shakes << " shakes" << endl;

  my_shakes[thread]=0;

  while (shakes<total_shakes) {

    //cout << thread << ":" << shake << " - shuffle cubes" << endl;
    std::random_shuffle(perm[thread].begin(),perm[thread].end());

    //cout << thread << ":" << shake << " - roll cubes" << endl;
    for (i=0; i<rows; i++)
      for (j=0; j<columns; j++) {
	//k = dist(gen);
	//cout << thread << ": " << i << "," << j << endl;
	//cout << thread << ": " << perm[thread].at(i+4*j) << endl;
	grid[thread][i][j] = cubes[perm[thread].at(i+4*j)][dist(gen)];
      }

    //cout << thread << ":" << shake << " - initialize" << endl;
    for (i=0; i<rows; i++)
      for (j=0; j<columns; j++)
	used[thread][i][j] = false;

    total[thread] = 0;

    //cout << thread << ":" << shake << " - find all" << endl;
    for (i=0; i<rows; i++)
      for (j=0; j<columns; j++) {
	used[thread][i][j] = true;
	find_all(dawg->letters[grid[thread][i][j]],i,j,thread);
	used[thread][i][j] = false;
      }

    //cout << thread << ":" << shake << " - reset" << endl;
    for (i=0; i<total[thread]; i++)
      found[thread][i]->unused = true;

    my_shakes[thread]++;
    __sync_fetch_and_add(&shakes,1);

  }

  //cout << thread << ": done simming " << shakes << " shakes" << endl;

}

int main(int argc, char* argv[]){

  string cube;

  //rows = strtol(argv[1],NULL,10);
  //columns = strtol(argv[2],NULL,10);

  int i;
  int j;

  ifstream cube_file;
  cube_file.open(argv[1]);

  if (cube_file) {
    i=0;
    while (cube_file >> cube) {
      for (j = 0; j < cube.length(); j++)
	cubes[i][j] = cube[j]-'a';

      //cout << cube << endl;
      /*for (j = 0; j < cube.length(); j++)
	cout << cubes[i][j] << ' ';
	cout << endl;*/

      i++;
    };
  }
  cube_file.close();

  ifstream word_file;
  word_file.open(argv[2]);

  node *dawg[threads];

  int thread;
  for (thread=0; thread<threads; thread++) {
    dawg[thread] = new node;
    dawg[thread]->value = "*";
    build_dawg(dawg[thread],&word_file);
    word_file.clear();
    word_file.seekg(0, ios::beg);
  }

  word_file.close();

  pthread_t iThreadId[threads];
  int iReturnValue[threads];

  arg_struct args[threads];

  for (thread=0; thread<threads; thread++) {
    for (int i=0; i<n_cubes; ++i)
      perm[thread].push_back(i);
  }

  //shakes=0;

  for (thread=0; thread<threads; thread++) {
    args[thread].dawg = dawg[thread];
    //args[thread].shakes = (total_shakes/threads);
    args[thread].thread = thread;
    //cout << thread << ": begin" << endl;
    iReturnValue[thread] = pthread_create(&iThreadId[thread], NULL, &sim, (void *)&args[thread]);
    //cout << thread << ": end" << endl;
  }

  for (thread=0; thread<threads; thread++) {
    pthread_join(iThreadId[thread], NULL);
    cout << thread << ": " << my_shakes[thread] << endl;
    //print_dawg(dawg[thread]);
  }

  return 0;

}
