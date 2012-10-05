using System;
using System.Collections.Generic;
using System.Drawing;

using Gtk;

namespace FRepDesigner
{

    /// <summary>
    /// Main window.
    /// </summary>
    public partial class MainWindow: Gtk.Window
    {
      public Scene Model;
      public RayTracingView View;
      Random rand = new Random(5);
      
      public MainWindow(): base(Gtk.WindowType.Toplevel)
      {
        // That's a hack because of the designer. If one needs to attach an event the designer attaches
        // it in the end of the file after the call to Initialize. Works for the most of the events
        // but not for events like Realize which happen in the initialization. This function is used
        // to attach the event handlers before the initialization part.
        PreBuild();
        Build();
      }

      protected virtual void PreBuild()
      {
        this.Realized += new global::System.EventHandler(this.OnRealized);
      }

      protected void OnRealized(object sender, System.EventArgs e)
      {
        // Init enviroment
        Model = new Scene();
        View = new RayTracingView();
        Model.Changed += SceneChanged;
      }

      protected void OnDeleteEvent(object sender, DeleteEventArgs a)
      {
        Application.Quit();
        a.RetVal = true;
      }

      protected void OnExitActionActivated(object sender, System.EventArgs e) {
        Application.Quit();
      }

      protected void OnOpenActionActivated(object sender, System.EventArgs e)
      {
            var fc = new Gtk.FileChooserDialog ("Choose the file to open",
                                            this, Gtk.FileChooserAction.Open,
                                            "Cancel", Gtk.ResponseType.Cancel,
                                            "Open", Gtk.ResponseType.Accept);
            try {
                fc.SelectMultiple = true;
                fc.SetCurrentFolder (Environment.CurrentDirectory);
                if (fc.Run () == (int)Gtk.ResponseType.Accept) {
                    List<string> filesToLoad = new List<string> ();
                    filesToLoad.AddRange (fc.Filenames);
                    foreach (string fileName in fc.Filenames) {
                        ShowMessage(String.Format ("Loadding {0}...", fileName));
                    }
                }
            } finally {
              fc.Destroy();
            }
      }

      private void ShowMessage(string msg)
      {
        var msgBox = new Gtk.MessageDialog(null, Gtk.DialogFlags.Modal, Gtk.MessageType.Info,
                                           Gtk.ButtonsType.Ok, msg);
        msgBox.Run();
        msgBox.Destroy();
      }

      private void SceneChanged(Scene scene, EventArgs e)
      {
        if (image1.Pixmap == null) 
          image1.Pixmap = new Gdk.Pixmap(image1.ParentWindow, image1.Allocation.Width, image1.Allocation.Height);
        
        View.Render(Model, image1.Pixmap);
        image1.QueueDraw();
      }
      
      protected void OnSphereActionActivated(object sender, System.EventArgs e)
      {
            FRepSolid frs = new FRepSolid("sqr(x)+sqr(y)+sqr(z)-100");
            frs.Color = new Cairo.Color(rand.NextDouble(),rand.NextDouble(),rand.NextDouble(),1);
            Model.AddPrimitive(frs);

//            float cx, cy, r;
//            FRepSolid frs;
//
//            cx = 0;
//            cy = 0;
//            r = 100;
//            frs = new FRepSolid(String.Format("sqr(x-({0}f))+sqr(y-({1}f))+sqr(z)-({2}f)", cx, cy, r));
//            frs.Color = new Cairo.Color(rand.NextDouble(),rand.NextDouble(),rand.NextDouble(),1);
//            Model.AddPrimitive(frs);
//            
//            cx = 24;
//            cy = 0;
//            r = 80;
//            frs = new FRepSolid(String.Format("sqr(x-({0}f))+sqr(y-({1}f))+sqr(z)-({2}f)", cx, cy, r));
//            frs.Color = new Cairo.Color(rand.NextDouble(),rand.NextDouble(),rand.NextDouble(),1);
//            Model.AddPrimitive(frs);
//
//            cx = -27;
//            cy = 0;
//            r = 120;
//            frs = new FRepSolid(String.Format("sqr(x-({0}f))+sqr(y-({1}f))+sqr(z)-({2}f)", cx, cy, r));
//            frs.Color = new Cairo.Color(rand.NextDouble(),rand.NextDouble(),rand.NextDouble(),1);
//            Model.AddPrimitive(frs);
//
//            cx = 5;
//            cy = 20;
//            r = 100;
//            frs = new FRepSolid(String.Format("sqr(x-({0}f))+sqr(y-({1}f))+sqr(z)-({2}f)", cx, cy, r));
//            frs.Color = new Cairo.Color(rand.NextDouble(),rand.NextDouble(),rand.NextDouble(),1);
//            Model.AddPrimitive(frs);
        }
    }

}