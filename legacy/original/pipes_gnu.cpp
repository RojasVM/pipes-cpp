// pipes_gnu.cpp — pipes.sh-like (Linux): pre-run menu + deferred color + responsive

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
using namespace std;

// -- Directions (no 180° turns)
enum Direction { UP=0, RIGHT=1, DOWN=2, LEFT=3 };
static inline int  rnd(int n){ return rand()%n; }
static inline void sleep_ms(int ms){ this_thread::sleep_for(chrono::milliseconds(ms)); }
static inline Direction turn_left(Direction d){ return (Direction)((d+3)%4); }
static inline Direction turn_right(Direction d){ return (Direction)((d+1)%4); }

// -- Terminal (Linux raw, resize via SIGWINCH)
static bool g_resized=false;
static void on_resize(int){ g_resized=true; }

struct Term {
  int W=80, H=24;
  termios oldt{};
  void init(){
    signal(SIGWINCH, on_resize);
    tcgetattr(STDIN_FILENO,&oldt);
    termios raw=oldt; raw.c_lflag &= ~(ICANON|ECHO);
    tcsetattr(STDIN_FILENO,TCSANOW,&raw);
    int f = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, f | O_NONBLOCK);
    updateSize(); hideCursor();
  }
  void restore(){
    showCursor();
    tcsetattr(STDIN_FILENO,TCSANOW,&oldt);
    int f = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, f & ~O_NONBLOCK);
  }
  void updateSize(){ winsize w{}; ioctl(STDOUT_FILENO,TIOCGWINSZ,&w); W=w.ws_col; H=w.ws_row; }
  bool checkResize(){ if (g_resized){ g_resized=false; updateSize(); clear(); return true; } return false; }
  void clear(){ cout << "\033[2J\033[H" << flush; }
  void mv(int x,int y){ cout << "\033[" << (y+1) << ";" << (x+1) << "H"; }
  void hideCursor(){ cout << "\033[?25l"; }
  void showCursor(){ cout << "\033[?25h"; }
  bool kbhit(){ int ch=getchar(); if (ch!=EOF){ ungetc(ch,stdin); return true; } return false; }
  int  getch_now(){ int ch=getchar(); return (ch==EOF)? -1 : ch; }
} term;

// -- Glyph types (16-entry table index)
struct PipeType { array<string,16> g{}; };
static PipeType T[10];

static void init_types(){
  // Note: idx_from returns 1..16; slots 3,8,9,14 are unused -> " "
  T[0].g = { "┃","┏"," ","┓","┛","━","┓"," "," ","┗","┃","┛","┗"," ","┏","━" };
  T[1].g = { "│","╭"," ","╮","╯","─","╮"," "," ","╰","│","╯","╰"," ","╭","─" };
  T[2].g = { "│","┌"," ","┐","┘","─","┐"," "," ","└","│","┘","└"," ","┌","─" };
  T[3].g = { "║","╔"," ","╗","╝","═","╗"," "," ","╚","║","╝","╚"," ","╔","═" };
  T[4].g = { "|","+"," ","+","+","-","+"," "," ","+","|","+","+"," ","+","-" };
  T[5].g = { "|","/"," ","\\","\\","-","\\"," "," ","\\","|","\\","/"," ","/","-" };
  T[6].g = { ".","."," ",".",".",".","."," "," ",".",".",".","."," ",".","."
  };
  T[7].g = { ".","o"," ","o","o",".","o"," "," ","o",".","o","o"," ","o","." };
  T[8].g = { "|","-"," ","|","\\","-","\\"," "," ","\\","|","/","/"," ","-","-" };
  T[9].g = { "╿","┎"," ","┒","┛","╾","┒"," "," ","┖","╿","┛","┖"," ","┎","╾" };
}

// -- Turn index: (in → out) → 1..16
static inline int idx_from(Direction in, Direction out){
  if (in==UP   && out==UP)    return 1;
  if (in==UP   && out==RIGHT) return 2;
  if (in==UP   && out==LEFT)  return 4;
  if (in==RIGHT&& out==UP)    return 5;
  if (in==RIGHT&& out==RIGHT) return 6;
  if (in==RIGHT&& out==DOWN)  return 7;
  if (in==DOWN && out==RIGHT) return 10;
  if (in==DOWN && out==DOWN)  return 11;
  if (in==DOWN && out==LEFT)  return 12;
  if (in==LEFT && out==UP)    return 13;
  if (in==LEFT && out==DOWN)  return 15;
  if (in==LEFT && out==LEFT)  return 16;
  if (in==UP||in==DOWN) return (in==UP?1:11);
  return (in==RIGHT?6:16);
}

// -- Config (defaults as requested)
struct Config{
  int p=5, fps=75, straight=15;
  long long limit=1000;
  bool randomStart=false, noColor=false, keepOnEdge=false, vivid=true;
} cfg;

static vector<int> activeTypes, palette;

// -- ANSI color (bright 90–97, no bold)
static inline string ansi_color(int c){
  if (cfg.noColor) return "";
  int base = cfg.vivid ? 90 : 30;
  return "\033[" + to_string(base + (c & 7)) + "m";
}
static inline string ansi_reset(){ return cfg.noColor ? "" : "\033[0m"; }

// -- Pipe state (deferred palette change)
struct State{
  int x=0,y=0;
  Direction in=RIGHT, out=RIGHT;
  int colorIndex=1, typeIndex=0;
  int pendingColor=-1, pendingType=-1; // applied on next step
};

// -- Will leaving screen?
static inline bool would_exit(const State& s, Direction nd){
  int nx=s.x, ny=s.y;
  if (nd==UP) --ny; else if (nd==DOWN) ++ny; else if (nd==LEFT) --nx; else ++nx;
  return nx<0 || nx>=term.W || ny<0 || ny>=term.H;
}

// -- One step (decide → draw → move)
static long long drawn=0;
static void draw_step(State& s){
  // Apply deferred palette at cell start (prevents mid-line color swap)
  if (s.pendingColor>=0){ s.colorIndex=s.pendingColor; s.pendingColor=-1; }
  if (s.pendingType >=0){ s.typeIndex =s.pendingType;  s.pendingType =-1; }

  s.out = s.in;
  if (rnd(20) >= cfg.straight) s.out = (rnd(2)? turn_left(s.in): turn_right(s.in));

  if (would_exit(s, s.out)){
    if (!cfg.keepOnEdge){
      s.pendingColor = palette[rnd((int)palette.size())];
      s.pendingType  = rnd((int)activeTypes.size());
    }
    Direction L=turn_left(s.in), R=turn_right(s.in);
    bool okL=!would_exit(s,L), okR=!would_exit(s,R);
    if (okL && okR) s.out = (rnd(2)? L:R);
    else if (okL)   s.out = L;
    else if (okR)   s.out = R;
    else            s.out = s.in;
  }

  int idx = idx_from(s.in, s.out);
  const string& g = T[ activeTypes[s.typeIndex] ].g[idx-1];
  term.mv(s.x, s.y);
  cout << ansi_color(s.colorIndex) << g << ansi_reset();

  s.in = s.out;
  if (s.in==UP) --s.y; else if (s.in==DOWN) ++s.y; else if (s.in==LEFT) --s.x; else ++s.x;
  ++drawn;
}

// -- Runtime hotkeys (Shift)
static void handle_keys_once(){
  if (!term.kbhit()) return;
  int ch = term.getch_now(); if (ch==-1) return;
  if      (ch=='P') cfg.straight=min(15,cfg.straight+1);
  else if (ch=='O') cfg.straight=max(5, cfg.straight-1);
  else if (ch=='F') cfg.fps=min(100,cfg.fps+5);
  else if (ch=='D') cfg.fps=max(20, cfg.fps-5);
  else if (ch=='C') cfg.noColor=!cfg.noColor;
  else if (ch=='K') cfg.keepOnEdge=!cfg.keepOnEdge;
  else throw runtime_error("quit");
}

// -- Pre-run menu (keyboard only)
static void draw_menu(){
  term.clear();
  cout << "\n  PIPES (Linux) — pre-run menu (press Enter to start)\n\n";
  cout << "  A/Z  Pipes:            " << cfg.p << "\n";
  cout << "  S/X  Straight [5..15]: " << cfg.straight << "\n";
  cout << "  F/D  FPS [20..100]:    " << cfg.fps << "\n";
  cout << "  L/J  Limit chars:      " << (cfg.limit==0? string("infinite") : to_string(cfg.limit)) << "\n";
  cout << "  R    Random start:     " << (cfg.randomStart? "ON":"OFF") << "\n";
  cout << "  K    Keep on edge:     " << (cfg.keepOnEdge? "ON":"OFF") << "\n";
  cout << "  C    Color enabled:    " << (!cfg.noColor? "ON":"OFF") << "\n";
  cout << "  V    Vivid colors:     " << (cfg.vivid? "ON":"OFF") << "\n";
  cout << "  T    Type set:         " << (activeTypes.empty()?0:activeTypes.front()) << " (0..9)\n";
  cout << "\n  Enter to start  |  Esc/Q to quit\n";
  cout << flush;
}
static bool run_menu(){
  if (activeTypes.empty()) activeTypes={0};
  if (palette.empty())     palette={1,2,3,4,5,6,7,0};
  draw_menu();
  while (true){
    if (term.checkResize()) draw_menu();
    int ch = term.getch_now();
    if (ch == -1){ sleep_ms(10); continue; }
    if (ch=='\r' || ch=='\n') return true;
    if (ch==27 || ch=='q' || ch=='Q') return false;
    if (ch=='A' || ch=='a') cfg.p = max(1, cfg.p+1);
    else if (ch=='Z' || ch=='z') cfg.p = max(1, cfg.p-1);
    else if (ch=='S' || ch=='s') cfg.straight = min(15, cfg.straight+1);
    else if (ch=='X' || ch=='x') cfg.straight = max(5,  cfg.straight-1);
    else if (ch=='F' || ch=='f') cfg.fps = min(100, cfg.fps+5);
    else if (ch=='D' || ch=='d') cfg.fps = max(20,  cfg.fps-5);
    else if (ch=='R' || ch=='r') cfg.randomStart = !cfg.randomStart;
    else if (ch=='K' || ch=='k') cfg.keepOnEdge  = !cfg.keepOnEdge;
    else if (ch=='C' || ch=='c') cfg.noColor     = !cfg.noColor;
    else if (ch=='V' || ch=='v') cfg.vivid       = !cfg.vivid;
    else if (ch=='T' || ch=='t'){ int v=(activeTypes.empty()?0:activeTypes.front()); v=(v+1)%10; activeTypes={v}; }
    else if (ch=='L' || ch=='l'){ if (cfg.limit==0) cfg.limit=1000; else cfg.limit=min<long long>(cfg.limit*10, 1000000000LL); }
    else if (ch=='J' || ch=='j'){ if (cfg.limit==0) cfg.limit=1000; cfg.limit=max<long long>(cfg.limit/10,0LL); if (cfg.limit<10) cfg.limit=0; }
    draw_menu();
  }
}

// -- main
int main(){
  srand((unsigned)time(nullptr));
  init_types();
  activeTypes={0}; palette={1,2,3,4,5,6,7,0};

  term.init();
  if (!run_menu()){ term.restore(); term.clear(); return 0; }
  term.clear();

  vector<State> S(cfg.p);
  for (auto& s: S){
    s.colorIndex = palette[rnd((int)palette.size())];
    s.typeIndex  = rnd((int)activeTypes.size());
    s.in = (Direction)rnd(4);
    if (cfg.randomStart){ s.x=rnd(term.W); s.y=rnd(term.H); }
    else { s.x=term.W/2; s.y=term.H/2; }
  }

  long long last_reset=0;
  try{
    while (true){
      if (term.checkResize()){
        for (auto& s: S){ s.x=min(max(0,s.x),term.W-1); s.y=min(max(0,s.y),term.H-1); }
      }
      for (auto& s: S){
        draw_step(s);
        if (cfg.limit>0 && (drawn-last_reset)>=cfg.limit){ term.clear(); last_reset=drawn; }
      }
      handle_keys_once();
      cout << flush;
      sleep_ms(max(1, 1000/cfg.fps));
    }
  } catch (const runtime_error&) {}
  term.restore(); term.clear();
  cout << "Drawn: " << drawn << "\n";
  return 0;
}

// Compile (Linux):
// g++ -std=gnu++17 -O2 -pthread pipes_gnu.cpp -o pipes
