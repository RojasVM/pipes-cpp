// pipes.cpp — pipes.sh-like clone with interactive pre-run menu (Windows/Linux)

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
  #include <conio.h>
#else
  #include <sys/ioctl.h>
  #include <unistd.h>
  #include <termios.h>
  #include <fcntl.h>
  #include <signal.h>
#endif

using namespace std;

// ---- Directions ----
enum Direction { UP=0, RIGHT=1, DOWN=2, LEFT=3 };
static inline int rnd(int n){ return rand()%n; }
static inline void sleep_ms(int ms){ this_thread::sleep_for(chrono::milliseconds(ms)); }
static inline Direction turn_left(Direction d){ return (Direction)((d+3)%4); }
static inline Direction turn_right(Direction d){ return (Direction)((d+1)%4); }

// ---- Terminal I/O ----
#ifndef _WIN32
static bool g_resized=false;
static void on_resize(int){ g_resized=true; }
#endif

struct Term {
  int W=80, H=24;
#ifdef _WIN32
  HANDLE hout{}, hin{};
  void enableVT(){
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    DWORD m=0; hout=GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleMode(hout,&m)){ m|=ENABLE_VIRTUAL_TERMINAL_PROCESSING; SetConsoleMode(hout,m); }
    hin = GetStdHandle(STD_INPUT_HANDLE);
  }
#else
  termios oldt{};
#endif
  void init(){
#ifdef _WIN32
    enableVT();
#else
    signal(SIGWINCH, on_resize);
    tcgetattr(STDIN_FILENO,&oldt);
    termios raw=oldt; raw.c_lflag &= ~(ICANON|ECHO);
    tcsetattr(STDIN_FILENO,TCSANOW,&raw);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
#endif
    updateSize();
    hideCursor();
  }
  void restore(){
    showCursor();
#ifndef _WIN32
    tcsetattr(STDIN_FILENO,TCSANOW,&oldt);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
#endif
  }
  void updateSize(){
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hout,&csbi);
    W = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    H = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    winsize w{}; ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    W = w.ws_col; H = w.ws_row;
#endif
  }
  bool checkResize(){
#ifdef _WIN32
    int ow=W, oh=H; updateSize();
    if (ow!=W || oh!=H){ clear(); return true; }
    return false;
#else
    if (g_resized){ g_resized=false; updateSize(); clear(); return true; }
    return false;
#endif
  }
  void clear(){ cout << "\033[2J\033[H" << flush; }
  void mv(int x,int y){ cout << "\033[" << (y+1) << ";" << (x+1) << "H"; }
  void hideCursor(){ cout << "\033[?25l"; }
  void showCursor(){ cout << "\033[?25h"; }
  bool kbhit(){
#ifdef _WIN32
    return _kbhit();
#else
    int ch = getchar();
    if (ch!=EOF){ ungetc(ch,stdin); return true; }
    return false;
#endif
  }
  int getch_now(){
#ifdef _WIN32
    return _getch();
#else
    int ch = getchar();
    return (ch==EOF)? -1 : ch;
#endif
  }
} term;

// ---- Glyph types (16-entry table) ----
struct PipeType { array<string,16> g{}; };
static PipeType T[10];

static void init_types(){
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

// ---- Turn index: (in → out) → 1..16 ----
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

// ---- Config (defaults) ----
struct Config {
  int p=5;
  int fps=75;
  int straight=15;
  long long limit=1000;
  bool randomStart=false;
  bool noBold=true;
  bool noColor=false;
  bool keepOnEdge=false;
  bool vivid=true;
} cfg;

static vector<int> activeTypes;
static vector<int> palette;

static inline string ansi_color(int c){
  if (cfg.noColor) return "";
  const int base = cfg.vivid ? 90 : 30;
  const int code = base + (c & 7);
  return "\033[" + to_string(code) + "m";
}
static inline string ansi_reset(){ return cfg.noColor ? "" : "\033[0m"; }

// ---- Pipe state ----
struct State {
  int x=0, y=0;
  Direction in=RIGHT, out=RIGHT;
  int colorIndex=1;
  int typeIndex=0;
};

static inline bool would_exit(const State& s, Direction nd){
  int nx=s.x, ny=s.y;
  if (nd==UP) --ny; else if (nd==DOWN) ++ny; else if (nd==LEFT) --nx; else ++nx;
  return nx<0 || nx>=term.W || ny<0 || ny>=term.H;
}

// ---- Step: decide → draw → move ----
static long long drawn=0;
static void draw_step(State& s, const PipeType&){
  s.out = s.in;
  if (rnd(20) >= cfg.straight) s.out = (rnd(2)? turn_left(s.in): turn_right(s.in));
  if (would_exit(s, s.out)){
    if (!cfg.keepOnEdge){
      s.colorIndex = palette[rnd((int)palette.size())];
      s.typeIndex  = rnd((int)activeTypes.size());
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

// ---- Hotkeys during run ----
static void handle_keys_once(){
  if (!term.kbhit()) return;
  int ch = term.getch_now();
  if (ch==-1) return;
  if      (ch=='P') cfg.straight = min(15, cfg.straight+1);
  else if (ch=='O') cfg.straight = max(5,  cfg.straight-1);
  else if (ch=='F') cfg.fps      = min(100,cfg.fps+5);
  else if (ch=='D') cfg.fps      = max(20, cfg.fps-5);
  else if (ch=='B') cfg.noBold   = !cfg.noBold;
  else if (ch=='C') cfg.noColor  = !cfg.noColor;
  else if (ch=='K') cfg.keepOnEdge = !cfg.keepOnEdge;
  else throw runtime_error("quit");
}

// ---- Menu: set params without CLI ----
static void draw_menu(){
  term.clear();
  cout << "\n  PIPES — pre-run menu (press Enter to start)\n\n";
  cout << "  A/Z  Pipes:            " << cfg.p << "\n";
  cout << "  S/X  Straight [5..15]: " << cfg.straight << "\n";
  cout << "  F/D  FPS [20..100]:    " << cfg.fps << "\n";
  cout << "  L/J  Limit chars:      " << (cfg.limit==0? string("infinite") : to_string(cfg.limit)) << "\n";
  cout << "  R    Random start:     " << (cfg.randomStart? "ON":"OFF") << "\n";
  cout << "  K    Keep on edge:     " << (cfg.keepOnEdge? "ON":"OFF") << "\n";
  cout << "  C    Color enabled:    " << (!cfg.noColor? "ON":"OFF") << "\n";
  cout << "  V    Vivid colors:     " << (cfg.vivid? "ON":"OFF") << "\n";
  cout << "  T    Type set:         " << activeTypes.front() << " (0..9)\n";
  cout << "\n  Enter to start  |  Esc/Q to quit\n";
  cout << flush;
}

static bool run_menu(){
  activeTypes = {0};
  palette     = {1,2,3,4,5,6,7,0};
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
    else if (ch=='T' || ch=='t'){ int v=activeTypes.front(); v=(v+1)%10; activeTypes[0]=v; }
    else if (ch=='L' || ch=='l'){
      if (cfg.limit==0) cfg.limit=1000; else cfg.limit = min<long long>(cfg.limit*10, 1000000000LL);
    } else if (ch=='J' || ch=='j'){
      if (cfg.limit==0) cfg.limit=1000;
      cfg.limit = max<long long>( (cfg.limit/10), 0LL );
      if (cfg.limit<10) cfg.limit=0;
    }
    draw_menu();
  }
}

// ---- Help ----
static void print_help(const char* prog){
  cout <<
"Usage: " << prog << " [no-args shows interactive menu]\n"
"-p N  -t SET ... -c COL ... -f FPS -s STR -r LIMIT -R -B -C -K -h -v\n";
}

// ---- main ----
int main(int argc, char** argv){
  ios::sync_with_stdio(false);
  cin.tie(nullptr);
  srand((unsigned)time(nullptr));

  init_types();
  activeTypes = {0};
  palette     = {1,2,3,4,5,6,7,0};

  // If any CLI flag was provided, skip menu and use CLI behavior.
  bool use_menu = (argc==1);

  for (int i=1;i<argc;i++){
    string a = argv[i];
    if (a=="-h"||a=="--help"){ print_help(argv[0]); return 0; }
    else if (a=="-v"){ cout << "pipes.cpp (pipes.sh-like with menu)\n"; return 0; }
    else if (a=="-p" && i+1<argc){ cfg.p = max(1, atoi(argv[++i])); use_menu=false; }
    else if (a=="-t" && i+1<argc){
      string v = argv[++i]; use_menu=false;
      if (!v.empty() && v[0]=='c'){
        string chars = v.substr(1);
        while ((int)chars.size()<16 && i+1<argc && argv[i+1][0]!='-') chars += argv[++i];
        if ((int)chars.size()<16){ cerr << "Error: -t c requires 16 chars.\n"; return 1; }
        PipeType custom{};
        for (int k=0;k<16;k++){ string s; s+=chars[k]; custom.g[k]=s; }
        custom.g[2]=" "; custom.g[7]=" "; custom.g[8]=" "; custom.g[13]=" ";
        T[0]=custom; activeTypes={0};
      } else {
        int tid = max(0, min(9, atoi(v.c_str())));
        activeTypes={tid};
      }
    }
    else if (a=="-c" && i+1<argc){ int col=(atoi(argv[++i])%8+8)%8; palette.push_back(col); use_menu=false; }
    else if (a=="-f" && i+1<argc){ cfg.fps = max(20, min(100, atoi(argv[++i]))); use_menu=false; }
    else if (a=="-s" && i+1<argc){ cfg.straight = max(5, min(15, atoi(argv[++i]))); use_menu=false; }
    else if (a=="-r"){ if (i+1<argc && argv[i+1][0]!='-') cfg.limit=atoll(argv[++i]); else cfg.limit=0; use_menu=false; }
    else if (a=="-R"){ cfg.randomStart=true; use_menu=false; }
    else if (a=="-B"){ cfg.noBold=true; use_menu=false; }
    else if (a=="-C"){ cfg.noColor=true; use_menu=false; }
    else if (a=="-K"){ cfg.keepOnEdge=true; use_menu=false; }
    else { cerr << "Unknown option: " << a << "\n"; return 1; }
  }

  Term t; term = t; term.init();
  if (use_menu){
    if (!run_menu()){
      term.restore(); term.clear(); return 0;
    }
  }
  term.clear();

  if (palette.empty()) palette = {1,2,3,4,5,6,7,0};
  if (activeTypes.empty()) activeTypes={0};

  vector<State> S(cfg.p);
  for (auto& s: S){
    s.colorIndex = palette[rnd((int)palette.size())];
    s.typeIndex  = rnd((int)activeTypes.size());
    s.in = (Direction)rnd(4);
    if (cfg.randomStart){ s.x=rnd(term.W); s.y=rnd(term.H); }
    else { s.x=term.W/2; s.y=term.H/2; }
  }

  long long last_reset = 0;
  try{
    while (true){
      if (term.checkResize()){
        for (auto& s: S){
          s.x = min(max(0,s.x), term.W-1);
          s.y = min(max(0,s.y), term.H-1);
        }
      }
      for (auto& s: S){
        draw_step(s, T[ activeTypes[s.typeIndex] ]);
        if (cfg.limit>0 && (drawn - last_reset) >= cfg.limit){
          term.clear(); last_reset = drawn;
        }
      }
      handle_keys_once();
      int ms = max(1, 1000 / cfg.fps);
      cout << flush;
      sleep_ms(ms);
    }
  } catch (const runtime_error&){}

  term.restore();
  term.clear();
  cout << "Drawn: " << drawn << "\n";
  return 0;
}
