// Archives a project as File, Archive Current Project does: args are the
// archive to write, the projects directory and the project's name.  The
// project is opened with no program open, so no buffer file is archived.
// @category SmartClock
import java.io.File;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;

import ghidra.app.script.GhidraScript;
import ghidra.base.project.GhidraProject;
import ghidra.framework.model.Project;
import ghidra.util.task.TaskMonitor;

public class ArchiveProject extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File out = new File(args[0]);
		GhidraProject ghidraProject = GhidraProject.openProject(args[1], args[2]);
		try {
			Project project = ghidraProject.getProject();
			Class<?> taskClass = Class.forName("ghidra.app.plugin.core.archive.ArchiveTask");
			Constructor<?> constructor = taskClass.getDeclaredConstructor(Project.class, File.class);
			constructor.setAccessible(true);
			Object task = constructor.newInstance(project, out);
			Method run = taskClass.getMethod("run", TaskMonitor.class);
			run.setAccessible(true);
			run.invoke(task, monitor);
			println("archived " + project.getName() + " to " + out + " (" + out.length() + " bytes)");
		}
		finally {
			ghidraProject.close();
		}
	}
}
