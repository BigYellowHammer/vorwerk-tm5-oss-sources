
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_java_awt_peer_gtk_GtkListPeer__
#define __gnu_java_awt_peer_gtk_GtkListPeer__

#pragma interface

#include <gnu/java/awt/peer/gtk/GtkComponentPeer.h>
#include <gcj/array.h>

extern "Java"
{
  namespace gnu
  {
    namespace java
    {
      namespace awt
      {
        namespace peer
        {
          namespace gtk
          {
              class GtkListPeer;
          }
        }
      }
    }
  }
  namespace java
  {
    namespace awt
    {
        class AWTEvent;
        class Dimension;
        class List;
    }
  }
}

class gnu::java::awt::peer::gtk::GtkListPeer : public ::gnu::java::awt::peer::gtk::GtkComponentPeer
{

public: // actually package-private
  virtual void create();
  virtual void create(jint);
  virtual void connectSignals();
public: // actually protected
  virtual void gtkWidgetModifyFont(::java::lang::String *, jint, jint);
public: // actually package-private
  virtual void gtkWidgetRequestFocus();
  virtual void getSize(jint, jint, JArray< jint > *);
public:
  GtkListPeer(::java::awt::List *);
public: // actually package-private
  virtual void append(JArray< ::java::lang::String * > *);
public:
  virtual void add(::java::lang::String *, jint);
  virtual void addItem(::java::lang::String *, jint);
  virtual void clear();
  virtual void delItems(jint, jint);
  virtual void deselect(jint);
  virtual ::java::awt::Dimension * getMinimumSize(jint);
  virtual ::java::awt::Dimension * getPreferredSize(jint);
  virtual JArray< jint > * getSelectedIndexes();
  virtual void makeVisible(jint);
  virtual ::java::awt::Dimension * minimumSize(jint);
  virtual ::java::awt::Dimension * preferredSize(jint);
  virtual void removeAll();
  virtual void select(jint);
  virtual void setMultipleMode(jboolean);
  virtual void setMultipleSelections(jboolean);
  virtual void handleEvent(::java::awt::AWTEvent *);
public: // actually protected
  virtual void postItemEvent(jint, jint);
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_java_awt_peer_gtk_GtkListPeer__
