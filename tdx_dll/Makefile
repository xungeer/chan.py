###############################################################################
# chan2026 - 缠论可视化分析系统 (通达信DLL接口)
# 基于 chan.py 核心算法
###############################################################################

# Configurations
INCLUDE=
CXFLAGS=$(INCLUDE) -O2 -std=c++11
LDFLAGS=-static

# Objectives
OBJECT1=Main.o CCentroid.o
TARGET1=chan2026-full.dll
OBJECTS=$(OBJECT1)
TARGETS=$(TARGET1)

# Build Commands
all : $(TARGETS)

$(TARGET1) : $(OBJECTS)
	@echo [LD] $@
	@g++ -shared -o $@ $^ $(LDFLAGS)

clean:
	@echo [RM] $(OBJECTS)
	-@del /f /q $(OBJECTS) $(TARGETS) 2>nul || true

# Compile rules
%.o : %.cpp
	@echo [CX] $<
	@g++ $(CXFLAGS) -c -o $@ $<

