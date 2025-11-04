# ====== Paths & dirs ======
OBJDIR := .obj
DEPDIR := .d
DATADIR := data

$(shell mkdir -p $(OBJDIR) > /dev/null)
$(shell mkdir -p $(DEPDIR) > /dev/null)
$(shell mkdir -p $(DATADIR) > /dev/null)

# ====== Compiler selection ======
# 默认用 clang++；你也可以 make CXX=g++ 覆盖
CXX ?= clang++

# 通用编译/链接选项
CXXFLAGS ?= -std=gnu++17 -Wall -Wextra -g -Ofast
LDFLAGS  ?=

# ====== If using clang++, point it to GCC-13's libstdc++ headers/libs ======
# 这三处就是你机器上的实际位置
GCC_VER ?= 13
GCC_INC1 := /usr/include/c++/$(GCC_VER)
GCC_INC2 := /usr/include/x86_64-linux-gnu/c++/$(GCC_VER)
GCC_LIB  := /usr/lib/gcc/x86_64-linux-gnu/$(GCC_VER)

ifeq ($(findstring clang++,$(CXX)),clang++)
  # 告诉 clang 使用 libstdc++ 并加上头文件与库搜索路径
  CXXFLAGS += -stdlib=libstdc++ -I$(GCC_INC1) -I$(GCC_INC2)
  LDFLAGS  += -stdlib=libstdc++ -L$(GCC_LIB)
endif

# ====== Targets ======
EXEC = htsim
SRCS = $(wildcard *.cpp)
OBJS = $(SRCS:%.cpp=$(OBJDIR)/%.o)

$(EXEC): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
POSTCOMPILE = @mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

$(OBJS): $(OBJDIR)/%.o: %.cpp
$(OBJS): $(OBJDIR)/%.o: %.cpp $(DEPDIR)/%.d
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@
	$(POSTCOMPILE)

$(DEPDIR)/%.d: ;

clean:
	rm -frv $(OBJDIR) $(DEPDIR) $(EXEC)

.PRECIOUS: $(DEPDIR)/%.d
.PHONY: clean

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS))))

