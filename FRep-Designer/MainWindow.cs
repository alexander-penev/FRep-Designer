using System;
using System.Collections.Generic;
using System.Collections;
using System.Drawing;

using Gtk;
using Gdk;

namespace FRepDesigner
{
    /// <summary>
    /// Main window.
    /// </summary>
    public partial class MainWindow: Gtk.Window
    {
        public string FileName = null; 
        public Scene Model;
        public RayTracingView View;
        public Gdk.Point mouse_point = new Gdk.Point(0,0);
        float mpoint_x;
        float mpoint_y;
        public Stack<Solid> Spheres = new Stack<Solid>();
        private List<RayTracingView> TracersList = new List<RayTracingView>(); 
        
        private Random rand = new Random(5);
             
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
            Model.Changed += SceneChanged;

            TracersList.Add(new SimpleRayTracingView());
            TracersList.Add(new SimpleMultithreadRayTracingView());
           // TracersList.Add(new OpenCLRayTracingView());
            
            foreach (var item in TracersList) {
                comboboxTracer.AppendText(item.GetType().Name);
            }
            comboboxTracer.Active = 0;

            View = TracersList[comboboxTracer.Active];
        }

        protected void OnDeleteEvent(object sender, DeleteEventArgs a)
        {
            Application.Quit();
            a.RetVal = true;
        }
        
        protected void OnOpenActionActivated(object sender, System.EventArgs e)
        {
            var fc = new Gtk.FileChooserDialog(
                "Choose the file to open",
                this, Gtk.FileChooserAction.Open,
                "Cancel", Gtk.ResponseType.Cancel,
                "Open", Gtk.ResponseType.Accept);
            
            try {
                fc.SelectMultiple = false;
                fc.SetCurrentFolder(Environment.CurrentDirectory);
                if (fc.Run() == (int)Gtk.ResponseType.Accept) {
                    FileName = fc.Filename;
                    ShowMessage(String.Format("Loadding '{0}'...", FileName));
                }
            } finally {
                fc.Destroy();
            }
        }

        protected void OnSaveAsActionActivated(object sender, EventArgs e)
        {
            var fc = new Gtk.FileChooserDialog (
                "Choose the filename to save",
                this, Gtk.FileChooserAction.Save,
                "Cancel", Gtk.ResponseType.Cancel,
                "Save", Gtk.ResponseType.Accept);
            
            try {
                fc.SelectMultiple = false;
                fc.SetCurrentFolder(Environment.CurrentDirectory);
                if (FileName != null) fc.CurrentName = FileName;
                if (fc.Run() == (int)Gtk.ResponseType.Accept) {
                    FileName = fc.Filename;
                    OnSaveActionActivated(sender, e);
                }
            } finally {
                fc.Destroy();
            }
        }
        
        protected void OnSaveActionActivated(object sender, EventArgs e)
        {
            if (FileName == null) {
                OnSaveAsActionActivated(sender, e);
            } else {
                //TODO: Save Model into file with name FileName
                ShowMessage(String.Format("Saving '{0}'...", FileName));
            }
        }

        public void UpdateView()
        {
            SceneChanged(Model, new EventArgs());
        }

        private void ShowMessage(string msg)
        {
            var msgBox = new Gtk.MessageDialog(null, Gtk.DialogFlags.Modal, Gtk.MessageType.Info, Gtk.ButtonsType.Ok, msg);
            msgBox.Run();
            msgBox.Destroy();
        }

        private void StatusMessage(string msg)
        {
            statusbar1.Pop(0);
            statusbar1.Push(0, msg);
        }

        private void SceneChanged(Scene scene, EventArgs e)
        {
            if (image1.Pixmap == null) {
                image1.Pixmap = new Gdk.Pixmap(image1.ParentWindow, image1.Allocation.Width, image1.Allocation.Height);
            } else {
                int w, h;
                image1.Pixmap.GetSize(out w, out h);
                if (image1.Allocation.Width != w || image1.Allocation.Height != h) {
                    image1.Pixmap = new Gdk.Pixmap(image1.ParentWindow, image1.Allocation.Width, image1.Allocation.Height);
                }
            }

            var sw = new System.Diagnostics.Stopwatch();
            sw.Start();
            View.Render(Model, image1.Pixmap);
            sw.Stop();
            StatusMessage(string.Format("Ray tracer: {0}; Render time: {1} ms; FPS: {2:N3}; RPS: {3:N3}", View.GetType().Name, sw.ElapsedMilliseconds, 1000.0/sw.ElapsedMilliseconds, (image1.Allocation.Width*image1.Allocation.Height)*1000.0/sw.ElapsedMilliseconds));
        
            image1.QueueDraw();
        }
        
        protected void OnSphereActionActivated(object sender, System.EventArgs e)
        {
            //FRepSolid frs = new FRepSolid("sqr(x-xc)+sqr(y-yc)+sqr(z-zc)-sqr(r)");
            FRepSolid frs = new FRepSolid("sqr(x)+sqr(y)+sqr(z)-100");
            frs.Color = new Cairo.Color(rand.NextDouble(),rand.NextDouble(),rand.NextDouble(),1);
            Model.AddPrimitive(frs);

//            return;
//
//            float cx, cy, r;
//            //FRepSolid frs;
//            float cx1, cy1, r1;
//
//            cx = -10;
//            cy = 5;
//            r = 100;
//            cx1 = 10;
//            cy1 = 5;
//            r1 = 90;
//            //frs = new FRepSolid(String.Format("sqr(x-({0}f))+sqr(y-({1}f))+sqr(z)-({2}f)", cx, cy, r));
//            frs = new FRepSolid(String.Format("max((sqr(x-({0}f))+sqr(y-({1}f))+sqr(z)-({2}f)),(sqr(x-({3}f))+sqr(y-({4}f))+sqr(z)-({5}f)))", cx, cy, r, cx1, cy1, r1));
//            frs.Color = new Cairo.Color(0,1,0);//rand.NextDouble(),rand.NextDouble(),rand.NextDouble(),1);
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
        protected void OnCustomFRepActionActivated(object sender, EventArgs e)
        {
            using (var inputDialog = new InputCustomFormulaDialog()) {
                if ((ResponseType)inputDialog.Run() == ResponseType.Ok) {
                    FRepSolid frs = new FRepSolid(inputDialog.Text);
                    frs.Color = new Cairo.Color(rand.NextDouble(),rand.NextDouble(),rand.NextDouble(),1);
                    Model.AddPrimitive(frs);
                }
                inputDialog.Destroy();
            }
        }
        
        protected void OnDialogQuestionActionActivated(object sender, EventArgs e)
        {
            //
        }
        
        protected void OnImage1SizeAllocated(object o, SizeAllocatedArgs args)
        {
            UpdateView();
        }
        
        protected void OnStopActionActivated(object sender, EventArgs e)
        {
            Application.Quit();
        }

        protected void OnNewActionActivated (object sender, EventArgs e)
        {
            Model = new Scene();
            Model.Changed += SceneChanged;
            UpdateView(); 
        }        
        protected void OnRFunctionActionActivated(object sender, EventArgs e)
        {
            throw new NotImplementedException ();
        }
        
        protected void OnZoomInActionActivated(object sender, EventArgs e)
        {
            throw new NotImplementedException ();
        }
        
        protected void OnZoomOutActionActivated(object sender, EventArgs e)
        {
            throw new NotImplementedException ();
        }
        
        protected void OnUndoActionActivated(object sender, EventArgs e)
        {
            throw new NotImplementedException ();
        }
        
        protected void OnPreferencesActionActivated(object sender, EventArgs e)
        {
            throw new NotImplementedException ();
        }
        
        protected void OnRedoActionActivated(object sender, EventArgs e)
        {
            throw new NotImplementedException ();
        }
        
        protected void OnSelectActionActivated(object sender, EventArgs e)
        {
            foreach(FRepSolid solid in Model.Solids)
            {
            }
        }
        
        protected void DrawBoundingBox ()
        {
            throw new NotImplementedException ();
        }
        
        protected void OnAboutActionActivated(object sender, EventArgs e)
        {
          throw new NotImplementedException ();
        }
        
        protected void OnComboboxTracerChanged(object sender, EventArgs e)
        {
            View = TracersList[comboboxTracer.Active];
            UpdateView();
        }
        
        
        protected void OnButtonPressEvent (object o, Gtk.ButtonPressEventArgs args)
        {
            int width, height;
            mpoint_x = Convert.ToSingle(args.Event.X);
            mpoint_y = Convert.ToSingle(args.Event.Y); 
            image1.Pixmap.GetSize(out width, out height);
            Ray3D ray = SimpleRayTracingView.ScreenDomainToWorldDomain(mpoint_x-(width/2), mpoint_y-(height/2));//align to the senter of rge WD origin
            Solid s;
            
            foreach(Solid solid in Model.Solids)
            { 
                View.Тrace(Model, ray, out s);
                if(s != null)
                {
                   solid.SetSelected(true);
                   UpdateView();
                }
                else 
                {
                    solid.SetSelected(false);                
                    UpdateView();
                }
            }
        }
    }
}
        