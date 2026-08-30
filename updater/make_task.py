import os
import subprocess
import sys
from pathlib import Path

TASK_NAME = "ARTPICST AutoUpdater"
SCRIPT_PATH = str(Path(__file__).resolve().parent / "auto_updater.py")
PYTHON_PATH = sys.executable


def build_task_command():
    return f'"{PYTHON_PATH}" "{SCRIPT_PATH}"'


def create_task():
    command = build_task_command()
    task_xml = f'''<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo>
    <Author>{os.environ.get("USERNAME", "ARTPICST")}</Author>
    <Description>Comprueba actualizaciones de ARTPICST cada 2 dias.</Description>
  </RegistrationInfo>
  <Triggers>
    <CalendarTrigger>
      <StartBoundary>2026-01-01T09:00:00</StartBoundary>
      <Enabled>true</Enabled>
      <ScheduleByDay>
        <DaysInterval>2</DaysInterval>
      </ScheduleByDay>
    </CalendarTrigger>
  </Triggers>
  <Principals>
    <Principal id="Author">
      <UserId>{os.environ.get("USERNAME", "SYSTEM")}</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>LeastPrivilege</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <AllowHardTerminate>true</AllowHardTerminate>
    <StartWhenAvailable>true</StartWhenAvailable>
    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>
    <Enabled>true</Enabled>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>{command}</Command>
    </Exec>
  </Actions>
</Task>
'''

    xml_path = str(Path(__file__).resolve().parent / "artpicst_updater_task.xml")
    with open(xml_path, "w", encoding="utf-16") as f:
        f.write(task_xml)

    subprocess.run(["schtasks", "/Create", "/TN", TASK_NAME, "/XML", xml_path, "/F"], check=True)
    print(f"Tarea creada: {TASK_NAME}")


if __name__ == "__main__":
    create_task()
