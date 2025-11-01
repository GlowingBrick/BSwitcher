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

            // 使用echo和nc与Unix socket通信
            const command = `echo '${escapedJson}' | nc -U ${this.socketPath}`;

            console.log('发送socket请求:', request);
            const result = await exec(command);

            if (result.errno === 0 && result.stdout) {
                try {
                    const response = JSON.parse(result.stdout);
                    console.log('收到socket响应:', response);
                    return response;
                } catch (parseError) {
                    console.error('解析socket响应失败:', parseError, '原始响应:', result.stdout);
                    throw new Error('无效的响应格式');
                }
            } else {
                console.error('Socket通信失败:', result.stderr);
                throw new Error(`通信失败: ${result.stderr}`);
            }
        } catch (error) {
            console.error('Socket通信错误:', error);
            throw error;
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
