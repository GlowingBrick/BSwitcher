// Unix Socket 客户端
export class SocketClient {
    constructor() {
        this.socketPath = '/dev/BSwitcher';
    }

    // 与后端通信
    async communicate(request) {
        try {
            const { exec } = await import('./ksu.js');

            // 转义JSON字符串用于shell命令
            const requestJson = JSON.stringify(request);
            const escapedJson = this.escapeForShell(requestJson);

            // 首先尝试使用 nc
            const ncCommand = `echo '${escapedJson}' | nc -U ${this.socketPath}`;
            
            console.log('尝试使用 nc 发送socket请求:', request);
            const ncResult = await exec(ncCommand);

            if (ncResult.errno === 0 && ncResult.stdout) {
                try {
                    const response = JSON.parse(ncResult.stdout);
                    console.log('通过 nc 收到socket响应:', response);
                    return response;
                } catch (parseError) {
                    console.error('解析socket响应失败:', parseError, '原始响应:', ncResult.stdout);
                    throw new Error('无效的响应格式');
                }
            } else {
                // nc 失败，尝试 socket_send
                console.log('nc 通信失败，尝试使用 socat:', ncResult.stderr);   //不是所有的netcat都是openbsd netcat
                return await this.trysocket_send(escapedJson, request);
            }
        } catch (error) {
            console.error('Socket通信错误:', error);
            throw error;
        }
    }

    // 尝试使用内建的 socket_send 通信
    async trysocket_send(escapedJson, originalRequest) {
        try {
            const { exec } = await import('./ksu.js');
            
            const socatCommand = `echo '${escapedJson}' | socket_send ${this.socketPath}`;
            
            console.log('尝试使用 socket_send 发送socket请求:', originalRequest);
            const socatResult = await exec(socatCommand);

            if (socatResult.errno === 0 && socatResult.stdout) {
                try {
                    const response = JSON.parse(socatResult.stdout);
                    console.log('通过 socket_send 收到socket响应:', response);
                    return response;
                } catch (parseError) {
                    console.error('解析socket_send响应失败:', parseError, '原始响应:', socatResult.stdout);
                    throw new Error('无效的响应格式');
                }
            } else {
                console.error('socket_send 通信也失败:', socatResult.stderr);
                throw new Error(`所有通信方式都失败: nc和socat都无法连接`);
            }
        } catch (error) {
            console.error('socket_send 通信错误:', error);
            throw new Error(`socket_send 通信失败: ${error.message}`);
        }
    }

    // 转义字符串用于shell命令
    escapeForShell(str) {
        return str.replace(/'/g, "'\\''")
        .replace(/\n/g, ' ')
        .replace(/\r/g, ' ');
    }

    // 读取调度配置
    async readScheduler() {
        const request = {
            target: 'scheduler',
            mode: 'read'
        };

        const response = await this.communicate(request);
        return response; // 直接返回完整的配置对象
    }

    // 写入调度配置
    async writeScheduler(config) {
        const request = {
            target: 'scheduler',
            mode: 'write',
            data: config
        };

        const response = await this.communicate(request);

        if (response.status === 'success') {
            return response;
        } else {
            throw new Error(response.message || '写入配置失败');
        }
    }

    // 读取应用列表
    async readAppList() {
        const request = {
            target: 'applist',
            mode: 'read'
        };

        const response = await this.communicate(request);
        return response; // 返回应用列表数组
    }
}
